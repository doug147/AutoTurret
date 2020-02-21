#pragma once
#include <Windows.h>
#include "API/ARK/Ark.h"
#include <stdlib.h>
#pragma comment(lib, "ArkApi.lib")

void FindAmmoCountOfInventory(UPrimalInventoryComponent* TheInventory, UClass* AmmoType, int& AmmoCount)
{
    for (auto item : TheInventory->InventoryItemsField())
        if (!item->bIsEngram().Get() && !item->bIsBlueprint().Get() && item->IsA(AmmoType))
            AmmoCount += item->ItemQuantityField();
}

bool FindTurretsByPlayer(std::vector<APrimalStructureTurret*> &OutTurrets, std::vector<UClass*>& OutAmmoTypes, AShooterPlayerController* PlayerController, float Radius, const FString &Filter = "", bool bForceReturn = false)
{
    TArray<AActor*> OutActors;
    UVictoryCore::ServerOctreeOverlapActors(
        &OutActors,
        ArkApi::GetApiUtils().GetWorld(),
        ArkApi::GetApiUtils().GetPosition(PlayerController),
        Radius,
        EServerOctreeGroup::STRUCTURES,
        false
    );

    for (auto&& Actor : OutActors)
    {
        if (!Actor->IsA(APrimalStructureItemContainer::GetPrivateStaticClass()) || Actor->TargetingTeamField() != PlayerController->TargetingTeamField())
            continue;

        auto Turret = (APrimalStructureTurret*)Actor;

        if (Turret->CurrentItemCountField() == Turret->MaxItemCountField() && !bForceReturn)
            continue;

        if (Turret->DescriptiveNameField().Find("Turret") == -1)
            continue;

        if (!Filter.IsEmpty() && Turret->DescriptiveNameField().Find(Filter) == -1)
            continue;

        OutTurrets.push_back(Turret);

        auto AllowedAmmoTypes = Turret->MyInventoryComponentField()->RemoteAddItemOnlyAllowItemClassesField();
        for (auto&& AmmoType : AllowedAmmoTypes)
            if (std::find(OutAmmoTypes.begin(), OutAmmoTypes.end(), AmmoType.uClass) == OutAmmoTypes.end())
                OutAmmoTypes.push_back(AmmoType.uClass);
    }

    Log::GetLog()->warn("Ammo Types: {}", OutAmmoTypes.size());
    Log::GetLog()->warn("Turrets: {}", OutTurrets.size());

    if (OutTurrets.size() > 0 && OutAmmoTypes.size() > 0)
        return true;

    return false;
}

void GetTurretsByAmmoType(const std::vector<APrimalStructureTurret*>& InTurrets, std::vector<APrimalStructureTurret*>& OutTurrets, UClass* AmmoType)
{
    for (auto&& turret : InTurrets)
    {
        for (auto&& ammotype : turret->MyInventoryComponentField()->RemoteAddItemOnlyAllowItemClassesField())
        {
            if (ammotype.uClass == AmmoType)
                OutTurrets.push_back(turret);
        }
    }
}

void RemoveAmmoFromInv(UPrimalInventoryComponent* Inv, UClass* AmmoType, int ToRemove)
{
    if (ToRemove <= 0)
        return;

    for (auto&& item : Inv->InventoryItemsField())
    {
        if (ToRemove <= 0)
            break;

        if (item->bIsEngram().Get() || item->bIsBlueprint().Get() || !item->IsA(AmmoType))
            continue;

        if (ToRemove >= item->ItemQuantityField())
        {
            ToRemove -= item->ItemQuantityField();
            Inv->RemoveItem(&item->ItemIDField(), false, false, true, true);
        }
        else
        {
            item->IncrementItemQuantity(-ToRemove, true, false, false, false, false);
            ToRemove -= ToRemove;
        }

        Inv->InventoryRefresh();
    }
}

void FillTurrets(const std::vector<APrimalStructureTurret*>& Turrets, std::vector<UClass*>& AmmoTypes, UPrimalInventoryComponent* TakeFromInventory, AShooterPlayerController* PlayerController)
{
    for (auto&& ammotype : AmmoTypes)
    {
        int AmmoCount = 0;
        FindAmmoCountOfInventory(TakeFromInventory, ammotype, AmmoCount);

        if (AmmoCount <= 0)
            continue;

        std::vector<APrimalStructureTurret*> OutTurrets;
        GetTurretsByAmmoType(Turrets, OutTurrets, ammotype);

        if (OutTurrets.size() <= 0)
            continue;

        if (AmmoCount < OutTurrets.size())
            continue;

        int AmmoUsed = 0;
        int AmmoEach = AmmoCount / OutTurrets.size();
        int MaxAmmoQty = ((UPrimalItem*)ammotype->GetDefaultObject(true))->MaxItemQuantityField();
        for (auto&& turret : OutTurrets)
        {
            int LocalAmmoUsed = 0;
            if (AmmoEach <= MaxAmmoQty)
            {
                UPrimalItem::AddNewItem(ammotype, turret->MyInventoryComponentField(), false, false, 0, false, AmmoEach, false, 0, true, nullptr, 0);
                AmmoUsed += AmmoEach;
                LocalAmmoUsed += AmmoEach;
            }
            else
            {
                for (int i = 0; i < AmmoEach / MaxAmmoQty; i++)
                {
                    if (turret->CurrentItemCountField() < turret->MaxItemCountField())
                    {
                        UPrimalItem::AddNewItem(ammotype, turret->MyInventoryComponentField(), false, false, 0, false, MaxAmmoQty, false, 0, true, nullptr, 0);
                        AmmoUsed += MaxAmmoQty;
                        LocalAmmoUsed += MaxAmmoQty;
                    }
                }

                int ExtraAmmoToAdd = AmmoEach % MaxAmmoQty;
                if (ExtraAmmoToAdd > 0)
                {
                    if (turret->CurrentItemCountField() < turret->MaxItemCountField())
                    {
                        UPrimalItem::AddNewItem(ammotype, turret->MyInventoryComponentField(), false, false, 0, false, ExtraAmmoToAdd, false, 0, true, nullptr, 0);
                        AmmoUsed += ExtraAmmoToAdd;
                        LocalAmmoUsed += ExtraAmmoToAdd;
                    }
                }
            }
            turret->RefreshInventoryItemCounts();
        }
        RemoveAmmoFromInv(TakeFromInventory, ammotype, AmmoUsed);
        ArkApi::GetApiUtils().SendChatMessage(PlayerController, L"Server", "<RichColor Color=\"0, 1, 0, 1\">Transferred {} {} each to {} turrets</>", AmmoEach, ((UPrimalItem*)ammotype->GetDefaultObject(true))->DescriptiveNameBaseField().ToString(), OutTurrets.size());
    }
}

void FillTurrets_Cmd(AShooterPlayerController* PlayerController, FString* message, int mode)
{
    if (!PlayerController->GetPlayerCharacter())
        return;

    TArray<FString> parsed;
    message->ParseIntoArray(parsed, L" ", true);

    std::vector<APrimalStructureTurret*> Turrets;
    std::vector<UClass*> AmmoTypes;
    bool bHasFoundTurrets = false;

    // No extra parameters
    if (parsed.Num() == 1)
    {
        bHasFoundTurrets = FindTurretsByPlayer(Turrets, AmmoTypes, PlayerController, 50000);

        if(bHasFoundTurrets)
            FillTurrets(Turrets, AmmoTypes, PlayerController->GetPlayerCharacter()->MyInventoryComponentField(), PlayerController);
    }
    // Turret type specified
    // For example the user may have typed, /fillturrets heavy
    else if (parsed.Num() == 2)
    {
        bHasFoundTurrets = FindTurretsByPlayer(Turrets, AmmoTypes, PlayerController, 50000, parsed[1]);

        if (bHasFoundTurrets)
            FillTurrets(Turrets, AmmoTypes, PlayerController->GetPlayerCharacter()->MyInventoryComponentField(), PlayerController);
    }

    if(!bHasFoundTurrets)
        ArkApi::GetApiUtils().SendChatMessage(PlayerController, L"Server", "<RichColor Color=\"1, 0, 0, 1\">No turrets in range that need filling!</>");
}

void Load()
{
    Log::Get().Init("AutoTurret Plugin");
    ArkApi::GetCommands().AddChatCommand("/fillturrets", &FillTurrets_Cmd);
}

void Unload()
{
    ArkApi::GetCommands().RemoveChatCommand("/fillturrets");
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        Load();
        break;
    case DLL_PROCESS_DETACH:
        Unload();
        break;
    }
    return TRUE;
}

