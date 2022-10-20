#include "Functions.h"

#include <iostream>

#include "MemoryManager.h"
#include "Game.h"

// Use them OUTSIDE main thread !
void Functions::pressKey(unsigned int key) {
	SendMessageW(ThreadSynchronizer::windowHandle, WM_KEYDOWN, key, 0);
}

void Functions::releaseKey(unsigned int key) {
	SendMessageW(ThreadSynchronizer::windowHandle, WM_KEYUP, key, 0);
}

unsigned long Functions::GetPlayerGuid() {
	typedef unsigned long func();
	func* function = (func*)GET_PLAYER_GUID_FUN_PTR;
	unsigned long guid = function();
	return guid;
}

void Functions::EnumerateVisibleObjects(int filter) {
	ListUnits.clear();
	for (int i = 0; i < 40; i++) GroupMembersIndex[i] = -1;
	typedef int (*EnumerateVisibleObjectsCallback)(unsigned long long, int);
	EnumerateVisibleObjectsCallback callback = &Callback;
	void* callbackPtr = (void*&)callback;

	typedef void __fastcall func(uintptr_t, int);
	func* function = (func*)ENUMERATE_VISIBLE_OBJECTS_FUN_PTR;
	try {
		function((uintptr_t)callbackPtr, filter);
	}
	catch (...) {}

	for (unsigned int i = 0; i < ListUnits.size(); i++) {
		ListUnits[i].unitReaction = localPlayer->getUnitReaction(ListUnits[i].Pointer);
	}
}

uintptr_t Functions::GetObjectPtr(unsigned long long guid) {
	typedef uintptr_t __stdcall func(unsigned long long);
	func* function = (func*)GET_OBJECT_PTR_FUN_PTR;
	uintptr_t ptr = function(guid);
	return ptr;
}

int Functions::Callback(unsigned long long guid, int filter) {
	uintptr_t pointer = Functions::GetObjectPtr(guid);
	if (pointer != 0) {
		ObjectType objectType = (*(ObjectType*)(pointer + OBJECT_TYPE_OFFSET));
		if (objectType == Unit || objectType == Player) {
			WoWUnit unit = WoWUnit(pointer, guid, objectType);
			ListUnits.push_back(unit);
			if (objectType == Player) {
				if (guid == GetPlayerGuid()) {
					if (localPlayer != NULL) delete(localPlayer);
					localPlayer = new LocalPlayer(pointer, guid, objectType);
					GroupMembersIndex[0] = ListUnits.size() - 1;
				}
				else {
					for (int i = 1; i <= NumGroupMembers; i++) {
						if (unit.name == Functions::UnitName(tarType + std::to_string(i)))
							GroupMembersIndex[i] = ListUnits.size() - 1;
					}
				}
			}
		}
	}
	return 1;
}

void Functions::LuaCall(const char* code) {
	typedef void __fastcall func(const char* code, const char* unused);
	func* function = (func*)LUA_CALL_FUN_PTR;

	function(code, "Unused");
}

uintptr_t Functions::GetText(const char* varName) {
	typedef uintptr_t __fastcall func(const char* varName, unsigned int nonSense, int zero);
	func* f = (func*)LUA_GET_TEXT_FUN_PTR;
	return f(varName, -1, 0);
}

void Functions::ClickAOE(Position position) {
	float* xyz = new float[3];
	xyz[0] = position.X; xyz[1] = position.Y; xyz[2] = position.Z;
	typedef void __fastcall func(float*);
	func* function = (func*)SPELL_C_HANDLETERRAINCLICK_FUN_PTR;
	function(xyz);
	delete[] xyz;
}

void Functions::ClassifyHeal() {
	//Heal all friendly players or NPC within 40 yards
	std::vector<float> PrctHp;
	HealTargetArray.clear();
	AoEHeal = 0;
	for (unsigned int i = 0; i < ListUnits.size(); i++) {
		if (ListUnits[i].unitReaction > Neutral && !ListUnits[i].isdead) {
			float dist = localPlayer->position.DistanceTo(ListUnits[i].position);
			if (dist < 60) {
				PrctHp.push_back(ListUnits[i].prctHP);
				if (ListUnits[i].objectType == Player && ListUnits[i].prctHP < 60) AoEHeal = AoEHeal + 1;
				HealTargetArray.push_back(i);
			}
		}
	}
	for (int i = PrctHp.size(); i > 0; i--) {
		for (int y = 0; y < i - 1; y++) {
			if (PrctHp[y] > PrctHp[y + 1]) {
				float tmp = PrctHp[y];
				PrctHp[y] = PrctHp[y + 1];
				PrctHp[y + 1] = tmp;
				int tmp2 = HealTargetArray[y];
				HealTargetArray[y] = HealTargetArray[y + 1];
				HealTargetArray[y + 1] = tmp2;
			}
		}
	}
}

Position meanPos(std::vector<Position> posArr) {
	Position clusterCenter = Position(0, 0, 0);
	for (unsigned int i = 0; i < posArr.size(); i++) {
		clusterCenter.X = clusterCenter.X + posArr[i].X;
		clusterCenter.Y = clusterCenter.Y + posArr[i].Y;
		clusterCenter.Z = clusterCenter.Z + posArr[i].Z;
	}
	clusterCenter.X = clusterCenter.X / posArr.size();
	clusterCenter.Y = clusterCenter.Y / posArr.size();
	clusterCenter.Z = clusterCenter.Z / posArr.size();
	return clusterCenter;
}

std::tuple<Position, int> Functions::getAOETargetPos(float range, float range2) {
	//Execution: ~0.05ms
	//Hierarchical Clustering implementation to find position
	std::vector<std::vector<Position>> clustersArr;
	std::vector<Position> clusters_center;
	//1- Chaque position est un cluster
	for (unsigned int i = 0; i < ListUnits.size(); i++) {
		if ((ListUnits[i].flags & UNIT_FLAG_IN_COMBAT) && (ListUnits[i].unitReaction) <= Neutral) {
			std::vector<Position> cluster;
			cluster.push_back(ListUnits[i].position);
			clustersArr.push_back(cluster);
			clusters_center.push_back(ListUnits[i].position);
		}
	}
	if (clusters_center.size() > 0) {
		//2- On rassemble les clusters les + proche et distance < range/2
		int min_cluster1, min_cluster2;
		do {
			float distMin = INFINITY;
			min_cluster1 = 0; min_cluster2 = 0;
			for (unsigned int i = 0; i < clustersArr.size(); i++) {
				for (unsigned int y = 0; y < clustersArr.size(); y++) {
					if (i != y) {
						float distCluster = clusters_center[i].DistanceTo(clusters_center[y]);
						if (distCluster < distMin && distCluster < (range / 2)) {
							distMin = distCluster;
							min_cluster1 = i;
							min_cluster2 = y;
						}
					}
				}
			}
			if (min_cluster1 != 0 || min_cluster2 != 0) {
				//On ajoute les positions du cluster_min2 dans cluster_min1
				for (unsigned int i = 0; i < clustersArr[min_cluster2].size(); i++) {
					clustersArr[min_cluster1].push_back(clustersArr[min_cluster2][i]);
				}
				clusters_center[min_cluster1] = meanPos(clustersArr[min_cluster1]);
				//On d�truit les valeurs du cluster_min2
				clustersArr.erase(clustersArr.begin() + min_cluster2);
				clusters_center.erase(clusters_center.begin() + min_cluster2);
			}
		} while (min_cluster1 != 0 || min_cluster2 != 0);
		//3- On choisit le cluster avec le plus d'unit�s
		unsigned int max_cluster_unit = 0; int index = 0;
		for (unsigned int i = 0; i < clustersArr.size(); i++) {
			if (clustersArr[i].size() > max_cluster_unit && clusters_center[i].DistanceTo(localPlayer->position) < range2) {
				max_cluster_unit = clustersArr[i].size();
				index = i;
			}
		}
		return std::make_tuple(clusters_center[index], max_cluster_unit);
	}
	else return std::make_tuple(Position(0, 0, 0), 0);
}

std::tuple<int, int, int, int> Functions::countEnemies() {
	std::vector<float> EnemyDistList;
	listIndexCloseEnemies.clear();
	for (int i = 0; i < 40; i++) {
		HasAggro[i].clear();
	}
	int nbr = 0, nbrClose = 0, nbrCloseFacing = 0, nbrEnemyPlayer = 0;
	ccTarget = NULL; bossFight = false;
	for (unsigned int i = 0; i < ListUnits.size(); i++) {
		if (ListUnits[i].unitReaction <= Neutral) { //Hostile
			if (ListUnits[i].level == -1 || ListUnits[i].level >= 62) bossFight = true;
			for (int y = 0; y <= NumGroupMembers; y++) { //Group member has aggro
				if ((GroupMembersIndex[y] > -1) && ListUnits[GroupMembersIndex[y]].Guid == ListUnits[i].targetGuid) HasAggro[y].push_back(ListUnits[i].Guid);
			}
			if (ListUnits[i].flags & UNIT_FLAG_PLAYER_CONTROLLED) { //Enemy player
				nbrEnemyPlayer++;
				if(targetUnit != NULL && ListUnits[i].Guid != targetUnit->Guid) {
					float dist = localPlayer->position.DistanceTo(ListUnits[i].position);
					if (dist < 30) ccTarget = &ListUnits[i]; //2nd Target for CC
				}
			}
			if ((ListUnits[i].flags & UNIT_FLAG_IN_COMBAT) && !(ListUnits[i].flags & UNIT_FLAG_CONFUSED)) { //Combat
				nbr++;
				EnemyDistList.push_back(localPlayer->position.DistanceTo(ListUnits[i].position));
				listIndexCloseEnemies.push_back(i);
				if (localPlayer->position.DistanceTo(ListUnits[i].position) < 8) {
					nbrClose++;
					if (localPlayer->isFacing(ListUnits[i].position, 0.5f)) {
						nbrCloseFacing++;
					}
				}
			}
		}
	}
	for (int i = listIndexCloseEnemies.size(); i > 0; i--) { //List based on closeness of enemies
		for (int y = 0; y < i - 1; y++) {
			if (EnemyDistList[y] > EnemyDistList[y + 1]) {
				float tmp = EnemyDistList[y];
				EnemyDistList[y] = EnemyDistList[y + 1];
				EnemyDistList[y + 1] = tmp;
				int tmp2 = listIndexCloseEnemies[y];
				listIndexCloseEnemies[y] = listIndexCloseEnemies[y + 1];
				listIndexCloseEnemies[y + 1] = tmp2;
			}
		}
	}
	return std::make_tuple(nbr, nbrClose, nbrCloseFacing, nbrEnemyPlayer);
}

int Functions::getNbrCreatureType(int range, CreatureType type1, CreatureType type2, CreatureType type3) {
	CreatureType args[3] = { type1, type2, type3 };
	int nbr = 0;
	for (unsigned int i = 0; i < ListUnits.size(); i++) {
		float dist = localPlayer->position.DistanceTo(ListUnits[i].position);
		if (dist < range) {
			for (int y = 0; y < 3; y++) {
				if (ListUnits[i].creatureType == args[y]) nbr++;
			}
		}
	}
	return nbr;
}

int Functions::GetBuffKey(int* IDs, int size) {
	//Retourne le joueur auquel il manque le buff
	for (int i = 1; i <= NumGroupMembers; i++) {
		if ((GroupMembersIndex[i] > -1) && (ListUnits[GroupMembersIndex[i]].unitReaction >= Friendly) && !ListUnits[GroupMembersIndex[i]].isdead && (localPlayer->position.DistanceTo(ListUnits[GroupMembersIndex[i]].position) < 40.0f)) {
			if (!ListUnits[GroupMembersIndex[i]].hasBuff(IDs, size)) return i;
		}
	}
	return 0;
}

bool Functions::PlayerIsRanged() {
	if(playerClass == "Mage" || playerClass == "Priest" || playerClass == "Warlock" || playerClass == "Hunter"
		|| (playerClass == "Druid" && (playerSpec == 0 || playerSpec == 2)) || (playerClass == "Paladin" && playerSpec == 0 && bossFight) || (playerClass == "Shaman" && (playerSpec == 0 || playerSpec == 2))) return true;
	else return false;
}

int Functions::getTankIndex() {
	for (int i = 1; i <= NumGroupMembers; i++) {
		if ((GroupMembersIndex[i] > -1) && ListUnits[GroupMembersIndex[i]].name == tankName) return i;
	}
	return 0;
}

void Functions::MoveToAlly(int unitIndex) {
	if (Moving == 0 || Moving == 2 || Moving == 5) {
		localPlayer->ClickToMove(Move, ListUnits[unitIndex].Guid, ListUnits[unitIndex].position);
		Moving = 5;
	}
}

//======================================================================//
//========================   Game Functions   ==========================//
//======================================================================//

int GetIntFromChar(const char* txt) {
	//Obtient le !premier! nombre d'un char*
	int length = strlen(txt); bool neg = false;
	int ret = 0; int y = 0; int lenNbr = 0;
	for (int i = 0; i < length; i++) {
		if (lenNbr == 0 && txt[i] == '-') neg = true;
		if ((int)txt[i] >= '0' && (int)txt[i] <= '9') lenNbr++;
		else if (lenNbr > 0) break;
	}
	for (int i = 0; i < length; i++) {
		if ((int)txt[i] >= '0' && (int)txt[i] <= '9') {
			ret = ret + (pow(10, lenNbr - y - 1) * ((int)txt[i] - '0'));
			y++;
		}
		else if (y > 0) break;
	}
	if (neg) return -ret;
	else return ret;
}

float GetFloatFromChar(const char* txt) {
	int length = strlen(txt); int y = 0;
	float ret = 0; int indDecimal = 0;
	for (int i = 0; i < length; i++) {
		if (txt[i] == ',' || txt[i] == '.') indDecimal = i;
	}
	for (int i = 0; i < length; i++) {
		if ((int)txt[i] >= '0' && (int)txt[i] <= '9') {
			if(indDecimal > 0)
				ret = ret + (pow(10, length - (length - indDecimal) - y - 1) * (float)((int)txt[i] - '0'));
			else
				ret = ret + (pow(10, length - y - 1) * (float)((int)txt[i] - '0'));
			y++;
		}
	}
	return ret;
}

int Functions::GetNumGroupMembers() {
	LuaCall("count = GetNumRaidMembers()");
	int nbrRaid = GetIntFromChar((char*)GetText("count"));
	LuaCall("count = GetNumPartyMembers()");
	int nbrParty = GetIntFromChar((char*)GetText("count"));
	if (nbrRaid > nbrParty) return nbrRaid;
	else return nbrParty;
}

bool Functions::IsInGroup() {
	LuaCall("count = GetNumPartyMembers()");
	int nbrParty = GetIntFromChar((char*)GetText("count"));
	if (nbrParty > 0) return true;
	else return false;
}

bool Functions::IsInRaid() {
	LuaCall("count = GetNumRaidMembers()");
	int nbrRaid = GetIntFromChar((char*)GetText("count"));
	if (nbrRaid > 0) return true;
	else return false;
}

int Functions::GetMerchantNumItems() {
	LuaCall("count = GetMerchantNumItems()");
	int nbr = GetIntFromChar((char*)GetText("count"));
	return nbr;
}

int Functions::GetRepairAllCost() {
	LuaCall("cost = GetRepairAllCost()");
	int cost = GetIntFromChar((char*)GetText("cost"));
	return cost;
}

bool Functions::HasPetUI() {
	LuaCall("UI = HasPetUI()");
	int hasUI = GetIntFromChar((char*)GetText("UI"));
	if (hasUI == 1) return true;
	else return false;
}

int Functions::GetPetHappiness() {
	LuaCall("hap = GetPetHappiness()");
	int hapiness = GetIntFromChar((char*)GetText("hap"));
	return hapiness;
}

void Functions::SellUselessItems() {
	for (int i = 0; i <= 4; i++) {
		for (int y = 1; y < GetContainerNumSlots(i); y++) {
			if (GetItemQuality(i, y) == 0) {
				std::string command = "UseContainerItem(" + std::to_string(i) + ", " + std::to_string(y) + ")";
				LuaCall(command.c_str());
				return;
			}
		}
	}
}

//======================================================================//
//============================   Timer/CD   ============================//
//======================================================================//

float Functions::GetTime() {
	LuaCall("time = GetTime()");
	float time = GetFloatFromChar((char*)GetText("time"));
	return time;
}

float Functions::GetItemCooldownDuration(int item_id) {
	for (int i = 0; i <= 4; i++) {
		for (int y = 1; y < GetContainerNumSlots(i); y++) {
			std::string item_link = GetContainerItemLink(i, y);
			int link_nbr = GetIntFromChar(item_link.c_str());
			if (link_nbr == item_id) {
				std::string command = "start, duration = GetContainerItemCooldown(" + std::to_string(i) + + ", " + std::to_string(y) + ")";
				LuaCall(command.c_str());
				float start = GetFloatFromChar((char*)GetText("start"));
				float duration = GetFloatFromChar((char*)GetText("duration"));
				float cdLeft = start + duration - GetTime();
				if (cdLeft < 0) cdLeft = 0;
				return cdLeft;
			}
		}
	}
	return 999;
}

float Functions::GetActionCooldownDuration(int slot) {
	std::string command = "start, duration = GetActionCooldown(" + std::to_string(slot) + ")";
	LuaCall(command.c_str());
	float start = GetFloatFromChar((char*)GetText("start"));
	float duration = GetFloatFromChar((char*)GetText("duration"));
	float cdLeft = start + duration - GetTime();
	if (cdLeft < 0) cdLeft = 0;
	return cdLeft;
}

float Functions::GetSpellCooldownDuration(std::string spell_name) {
	int spell_id;
	std::tie(spell_id, std::ignore) = GetSpellID(spell_name);
	if (spell_id > 0) {
		std::string command = "start, duration = GetSpellCooldown(" + std::to_string(spell_id) + ", BOOKTYPE_SPELL)";
		LuaCall(command.c_str());
		float start = GetFloatFromChar((char*)GetText("start"));
		float duration = GetFloatFromChar((char*)GetText("duration"));
		float cdLeft = start + duration - GetTime();
		if (cdLeft < 0) cdLeft = 0;
		return cdLeft;
	} else return 999;
}

//======================================================================//
//=============================   Items   ==============================//
//======================================================================//

int Functions::GetContainerNumSlots(int slot) {
	std::string command = "slots = GetContainerNumSlots(" + std::to_string(slot) + ")";
	LuaCall(command.c_str());
	int slots = GetIntFromChar((char*)GetText("slots"));
	return slots;
}

std::tuple<std::string, int> Functions::GetContainerItemInfo(int bag, int slot) {
	std::string command = "texture, itemCount = GetContainerItemInfo("+std::to_string(bag)+", "+std::to_string(slot)+")";
	LuaCall(command.c_str());
	std::string texture = (char*)GetText("texture");
	int itemCount = GetIntFromChar((char*)GetText("itemCount"));
	return std::make_tuple(texture, itemCount);
}

std::string Functions::GetContainerItemLink(int bag, int slot) {
	std::string command = "link = GetContainerItemLink(" + std::to_string(bag) + ", " + std::to_string(slot) + ")";
	LuaCall(command.c_str());
	std::string link = (char*)GetText("link");
	return link;
}

bool Functions::IsInventoryFull() {
	for (int i = 0; i <= 4; i++) {
		for (int y = 1; y < GetContainerNumSlots(i); y++) {
			std::string texture;
			std::tie(texture, std::ignore) = GetContainerItemInfo(i, y);
			if (texture == "") return false;
		}
	}
	return true;
}

int Functions::GetItemCount(std::string item_info) {
	//Trouve par le nom la quantit� d'item similaire dans l'inventaire
	int total = 0;
	for (int i = 0; i <= 4; i++) {
		for (int y = 1; y < GetContainerNumSlots(i); y++) {
			std::string item_link = GetContainerItemLink(i, y);
			if (item_link.find(item_info) != std::string::npos) {
				int itemCount;
				std::tie(std::ignore, itemCount) = GetContainerItemInfo(i, y);
				total = total + itemCount;
			}
		}
	}
	return total;
}

int Functions::GetItemCount(int item_id) {
	//Trouve par l'ID la quantit� d'item similaire dans l'inventaire
	int total = 0;
	for (int i = 0; i <= 4; i++) {
		for (int y = 1; y < GetContainerNumSlots(i); y++) {
			std::string item_link = GetContainerItemLink(i, y);
			int link_nbr = GetIntFromChar(item_link.c_str());
			if (link_nbr == item_id) {
				int itemCount;
				std::tie(std::ignore, itemCount) = GetContainerItemInfo(i, y);
				total = total + itemCount;
			}
		}
	}
	return total;
}

int Functions::GetItemQuality(int bag, int slot) {
	std::string item_link = GetContainerItemLink(bag, slot);
	if (item_link == "") return -1;
	else if (item_link.find("9d9d9d") != std::string::npos) return 0;	//Poor
	else if (item_link.find("ffffff") != std::string::npos) return 1;	//Common
	else if (item_link.find("1eff00") != std::string::npos) return 2;	//Uncommon
	else if (item_link.find("0070dd") != std::string::npos) return 3;	//Rare
	else if (item_link.find("a335ee") != std::string::npos) return 4;	//Epic
	else if (item_link.find("ff8000") != std::string::npos) return 5;	//Legendary
	else if (item_link.find("e6cc80") != std::string::npos) return 6;	//Artifact
	else return -1;
}

void Functions::PickupItem(std::string item_info) {
	for (int i = 0; i <= 4; i++) {
		for (int y = 1; y < GetContainerNumSlots(i); y++) {
			std::string item_link = GetContainerItemLink(i, y);
			if (item_link.find(item_info) != std::string::npos) {
				std::string command = "PickupContainerItem(" + std::to_string(i) + ", " + std::to_string(y) + ")";
				LuaCall(command.c_str());
			}
		}
	}
}

void Functions::PlaceItem(int slot, std::string itemName) {
	//Place l'objet dans le slot indiqu�
	PickupItem(itemName);
	std::string command = "PlaceAction(" + std::to_string(slot) + ")";
	LuaCall((command + " ClearCursor()").c_str());
}

void Functions::PickupItem(int item_id) {
	for (int i = 0; i <= 4; i++) {
		for (int y = 1; y < GetContainerNumSlots(i); y++) {
			std::string item_link = GetContainerItemLink(i, y);
			int link_nbr = GetIntFromChar(item_link.c_str());
			if (link_nbr == item_id) {
				std::string command = "PickupContainerItem(" + std::to_string(i) + ", " + std::to_string(y) + ")";
				LuaCall(command.c_str());
			}
		}
	}
}

void Functions::PlaceItem(int slot, int item_id) {
	//Place l'objet dans le slot indiqu�
	PickupItem(item_id);
	std::string command = "PlaceAction(" + std::to_string(slot) + ")";
	LuaCall((command + " ClearCursor()").c_str());
}

int Functions::HasDrink() {
	int listID[20] = { 159, 1179, 1205, 1645, 1708, 2136, 2288, 3772, 4791, 5350, 8077
		, 8078, 8079, 8766, 9451, 10841, 13724, 18300, 19301, 20031 };
	for (int i = 0; i < 20; i++) {
		if (GetItemCount(listID[i]) > 0) return listID[i];
	}
	return 0;
}

int Functions::HasMeat() {
	int CookedlistID[47] = { 117, 724, 1017, 2287, 2679, 2680, 2681, 2684, 2685, 2687, 2888, 3220, 3662, 3664, 3726, 3727, 3728, 3770, 3771, 4457, 4599, 5472, 5474, 5477, 5478, 5479, 7097, 8952, 11444, 12209, 12210, 12211, 12213, 12215, 12216, 12224, 13851, 17119, 17222, 17407, 17408, 18045, 19224, 19304, 19305, 20074, 21023 };
	for (int i = 0; i < 47; i++) {
		if (GetItemCount(CookedlistID[i]) > 0) return CookedlistID[i];
	}
	return 0;
}

bool Functions::HasHPotion() {
	LuaCall("level = UnitLevel(\"player\")");
	int levelPlayer = GetIntFromChar((char*)GetText("level"));
	if (GetItemCount(118) > 0 && levelPlayer < 20) return true;
	else if (GetItemCount(858) > 0 && levelPlayer < 30) return true;
	else if (GetItemCount(929) > 0 && levelPlayer < 40) return true;
	else if (GetItemCount(1710) > 0 && levelPlayer < 50) return true;
	else if (GetItemCount(3928) > 0) return true;
	else if (GetItemCount(13446) > 0) return true;
	else return false;
}

float Functions::GetHPotionCD() {
	int listID[6] = { 118, 858, 929, 1710, 3928, 13446 };
	for (int i = 0; i < 6; i++) {
		if (GetItemCount(listID[i] > 0)) return GetItemCooldownDuration(listID[i]);
	}
	return 99999;
}

bool Functions::HasMPotion() {
	LuaCall("level = UnitLevel(\"player\")");
	int levelPlayer = GetIntFromChar((char*)GetText("level"));
	if (GetItemCount(2455) > 0 && levelPlayer < 25) return true;
	else if (GetItemCount(3385) > 0 && levelPlayer < 35) return true;
	else if (GetItemCount(3827) > 0 && levelPlayer < 45) return true;
	else if (GetItemCount(6149) > 0 && levelPlayer < 55) return true;
	else if (GetItemCount(13443) > 0) return true;
	else if (GetItemCount(13444) > 0) return true;
	else return false;
}

float Functions::GetMPotionCD() {
	int listID[6] = { 2455, 3385, 3827, 6149, 13443, 13444 };
	for (int i = 0; i < 6; i++) {
		if (GetItemCount(listID[i] > 0)) return GetItemCooldownDuration(listID[i]);
	}
	return 99999;
}

bool Functions::HasHealthstone() {
	LuaCall("level = UnitLevel(\"player\")");
	int levelPlayer = GetIntFromChar((char*)GetText("level"));
	if ((((GetItemCount(5512) > 0) || (GetItemCount(19004) > 0) || (GetItemCount(19005) > 0)) && levelPlayer < 30)) return true;
	else if ((((GetItemCount(5511) > 0) || (GetItemCount(19006) > 0) || (GetItemCount(19007) > 0)) && levelPlayer < 40)) return true;
	else if ((((GetItemCount(5509) > 0) || (GetItemCount(19008) > 0) || (GetItemCount(19009) > 0)) && levelPlayer < 50)) return true;
	else if (((GetItemCount(5510) > 0) || (GetItemCount(19010) > 0) || (GetItemCount(19011) > 0))) return true;
	else if (((GetItemCount(9421) > 0) || (GetItemCount(19012) > 0) || (GetItemCount(19013) > 0))) return true;
	else return false;
}

float Functions::GetHealthstoneCD() {
	int listID[15] = {5512, 19004, 19005, 5511, 19006, 19007, 5509, 19008, 19009, 5510, 19010, 19011, 9421, 19012, 19013};
	for (int i = 0; i < 15; i++) {
		if (GetItemCount(listID[i] > 0)) return GetItemCooldownDuration(listID[i]);
	}
	return 99999;
}

//======================================================================//
//========================   Buffs / Debuffs   =========================//
//======================================================================//

std::string Functions::UnitBuff(std::string target, int index) {
	std::string command = "texture = UnitBuff(\"" + (std::string)target + "\", " + std::to_string(index) + ")";
	LuaCall(command.c_str());
	char* texture = (char*)GetText("texture");
	return texture;
}

std::tuple<std::string, int, std::string> Functions::UnitDebuff(std::string target, int index) {
	std::string command = "texture,count,type = UnitDebuff(\"" + (std::string)target + "\", " + std::to_string(index) + ")";
	LuaCall(command.c_str());
	char* texture = (char*)GetText("texture");
	int count = GetIntFromChar((char*)GetText("count"));
	char* type = (char*)GetText("type");
	return std::make_tuple(texture, count, type);
}

bool Functions::GetUnitBuff(std::string target, std::string texture) {
	for (int i = 1; i <= 30; i++) {
		std::string textname = UnitBuff(target, i);
		if (textname == texture) return true;
		else if (textname == "") return false;
	}
	return false;
}

bool Functions::GetUnitDebuff(std::string target, std::string texture) {
	for (int i = 1; i <= 16; i++) {
		std::string textname;
		std::tie(textname, std::ignore, std::ignore) = UnitDebuff(target, i);
		if (textname == texture) return true;
		else if (textname == "") return false;
	}
	return false;
}

int Functions::GetStackDebuff(std::string target, std::string texture) {
	for (int i = 1; i <= 16; i++) {
		int count;
		std::tie(std::ignore, count, std::ignore) = UnitDebuff(target, i);
		return count;
	}
	return 0;
}

int Functions::GetBuffKey(std::string buffTexture, std::string buffTexture2) {
	//Retourne le joueur auquel il manque le buff
	for (int i = 1; i <= NumGroupMembers; i++) {
		bool buffB = false;
		if (!UnitCanAttack("player", tarType+std::to_string(i)) && !UnitIsDeadOrGhost(tarType + std::to_string(i)) && CheckInteractDistance(tarType+std::to_string(i), 4)) {
			for (int y = 1; y < 40; y++) {
				std::string buff = UnitBuff(tarType + std::to_string(i), y);
				if (buff == buffTexture || buff == buffTexture2) buffB = true;
			}
			if (buffB == false) return i;
		}
	}
	return 0;
}

bool Functions::GetUnitDispel(std::string target, std::string dispellType1, std::string dispellType2, std::string dispellType3) {
	//Retourne si la cible a un debuff � dispel
	std::string args[3] = { dispellType1, dispellType2, dispellType3 };
	for (int i = 1; i <= 16; i++) {
		std::string debuffIcon, debuffType;
		std::tie(debuffIcon, std::ignore, debuffType) = UnitDebuff(target, i);
		if (debuffIcon != "Interface\\Icons\\Spell_Frost_FrostArmor02") {
			for (int y = 0; y < 3; y++) {
				if (args[y] == debuffType) return true;
			}
		}
	}
	return false;
}

int Functions::GetDispelKey(std::string dispellType1, std::string dispellType2, std::string dispellType3) {
	//Retourne le joueur du groupe � dispel
	for (int i = 1; i <= NumGroupMembers; i++) {
		if (GetUnitDispel(tarType+std::to_string(i), dispellType1, dispellType2, dispellType3) && CheckInteractDistance(tarType+std::to_string(i), 4)) return i;
	}
	return 0;
}

//======================================================================//
//=============================   Status   =============================//
//======================================================================//

bool Functions::IsSlowed(std::string target) {
	//Return if target is rooted
	std::vector<std::string> tab;
	tab.push_back("Interface\\Icons\\Spell_Frost_Glacier");
	tab.push_back("Interface\\Icons\\Ability_Rogue_Trip");
	for (int i = 1; i <= 16; i++) {
		std::string debuff;
		std::tie(debuff, std::ignore, std::ignore) = UnitDebuff(target, i);
		for (unsigned int y = 0; y < tab.size(); y++) {
			if (debuff == tab[y]) return true;
		}
	}
	return false;
}

bool Functions::IsRooted(std::string target) {
	//Return if target is rooted
	std::vector<std::string> tab;
	tab.push_back("Interface\\Icons\\Spell_Frost_FrostNova");
	tab.push_back("Interface\\Icons\\Spell_Frost_FrostArmor");
	tab.push_back("Interface\\Icons\\Spell_Nature_StrangleVines");
	tab.push_back("Interface\\Icons\\Ability_Ensnare");
	for (int i = 1; i <= 16; i++) {
		std::string debuff;
		std::tie(debuff, std::ignore, std::ignore) = UnitDebuff(target, i);
		for (unsigned int y = 0; y < tab.size(); y++) {
			if (debuff == tab[y]) return true;
		}
	}
	return false;
}

bool Functions::IsFeared(std::string target) {
	//Return if target is feared
	std::vector<std::string> tab;
	tab.push_back("Interface\\Icons\\Spell_Shadow_Possession");
	tab.push_back("Interface\\Icons\\Spell_Shadow_PsychicScream");
	tab.push_back("Interface\\Icons\\Spell_Shadow_DeathCoil");
	tab.push_back("Interface\\Icons\\Ability_GolemThunderClap");
	tab.push_back("Interface\\Icons\\Ability_Physical_Taunt");
	for (int i = 1; i <= 16; i++) {
		std::string debuff;
		std::tie(debuff, std::ignore, std::ignore) = UnitDebuff(target, i);
		for (unsigned int y = 0; y < tab.size(); y++) {
			if (debuff == tab[y]) return true;
		}
	}
	return false;
}

bool Functions::IsCharmed(std::string target) {
	//Return if target is charmed
	LuaCall("zoneText = GetRealZoneText()");
	std::string GetRealZoneText = (char*)GetText("zoneText");
	std::vector<std::pair<std::string, std::string>> tab;
	tab.push_back(std::make_pair("Interface\\Icons\\Spell_Shadow_ShadowWordDominate", ""));
	tab.push_back(std::make_pair("Interface\\Icons\\Spell_Shadow_MindSteal", ""));
	tab.push_back(std::make_pair("Interface\\Icons\\Spell_Shadow_GatherShadows", "Shadowfang Keep"));
	for (int i = 1; i <= 16; i++) {
		std::string debuff;
		std::tie(debuff, std::ignore, std::ignore) = UnitDebuff(target, i);
		for (unsigned int y = 0; y < tab.size(); y++) {
			if (debuff == tab[y].first && (tab[y].second == "" || tab[y].second == GetRealZoneText)) return true;
		}
	}
	return false;
}

//======================================================================//
//=========================   Spells/Actions   =========================//
//======================================================================//

int Functions::GetNumSpellTabs() {
	LuaCall("res = GetNumSpellTabs()");
	int result = GetIntFromChar((char*)GetText("res"));
	return result;
}

std::string Functions::GetSpellName(int id) {
	std::string command = "res = GetSpellName(" + std::to_string(id) + ", BOOKTYPE_SPELL)";
	LuaCall(command.c_str());
	char* result = (char*)GetText("res");
	return result;
}

std::string Functions::GetSpellTexture(int spellID) {
	std::string command = "res = GetSpellTexture(" + std::to_string(spellID) + ", BOOKTYPE_SPELL)";
	LuaCall(command.c_str());
	std::string result = (char*)GetText("res");
	return result;
}

std::tuple<std::string, std::string, int, int> Functions::GetSpellTabInfo(int index) {
	std::string command = "name, texture, offset, numSpells = GetSpellTabInfo(" + std::to_string(index) + ")";
	LuaCall(command.c_str());
	char* name = (char*)GetText("name");
	char* texture = (char*)GetText("texture");
	int offset = GetIntFromChar((char*)GetText("offset"));
	int numSpells = GetIntFromChar((char*)GetText("numSpells"));
	return std::make_tuple(name, texture, offset, numSpells);
}

std::tuple<int, int> Functions::GetSpellID(std::string spell_name) {
	//Execution = 1ms
	int id = 0; int rank = 1;
	for (int i = 1; i <= GetNumSpellTabs(); i++) {
		int numSpells;
		std::tie(std::ignore, std::ignore, std::ignore, numSpells) = GetSpellTabInfo(i);
		for (int y = 0; y < numSpells; y++) {
			id++;
			if (spell_name == GetSpellName(id)) {
				while (spell_name == GetSpellName(id + 1)) {
					id++; rank++;
				}
				return std::make_tuple(id, rank);
			}
		}
	}
	return std::make_tuple(0, 0);
}

bool Functions::IsPlayerSpell(std::string spell_name) {
	int spellID;
	std::tie(spellID, std::ignore) = GetSpellID(spell_name);
	if (spellID == 0) return false;
	else return true;
}

bool Functions::IsSpellReady(std::string spell_name) {
	//Execution: ~2.2ms
	int slot = GetSlot(spell_name);
	if (slot > 0) {
		if (IsUsableAction(slot) && (GetActionCooldownDuration(slot) <= 1.25)) {
			return true;
		}
	}
	return false;
}

void Functions::CastSpellByName(std::string spell_name) {
	LuaCall(("CastSpellByName(\"" + spell_name + "\")").c_str());
}

void Functions::UseAction(int slot, int self) {
	if(self == 1) LuaCall(("UseAction(" + std::to_string(slot) + ", 0, 1)").c_str());
	else LuaCall(("UseAction("+std::to_string(slot)+")").c_str());
}

bool Functions::IsAutoRepeatAction(int slot) {
	LuaCall(("autoR = IsAutoRepeatAction(" + std::to_string(slot) + ")").c_str());
	int autoR = GetIntFromChar((char*)GetText("autoR"));
	if (autoR == 1) return true;
	else return false;
}

bool Functions::IsUsableAction(int slot) {
	std::string command = "usable, nomana = IsUsableAction(" + std::to_string(slot) + ")";
	LuaCall(command.c_str());
	int usable = GetIntFromChar((char*)GetText("usable"));
	int nomana = GetIntFromChar((char*)GetText("nomana"));
	if (usable == 1 && nomana == 0) return true;
	else return false;
}

bool Functions::HasAction(int slot) {
	std::string command = "res = HasAction(" + std::to_string(slot) + ")";
	LuaCall(command.c_str());
	int result = GetIntFromChar((char*)GetText("res"));
	if (result == 1) return true;
	else return false;
}

std::string Functions::GetActionTexture(int slot) {
	std::string command = "res = GetActionTexture(" + std::to_string(slot) + ")";
	LuaCall(command.c_str());
	std::string result = (char*)GetText("res");
	return result;
}

bool Functions::IsConsumableAction(int slot) {
	std::string command = "res = IsConsumableAction(" + std::to_string(slot) + ")";
	LuaCall(command.c_str());
	int result = GetIntFromChar((char*)GetText("res"));
	if (result == 1) return true;
	else return false;
}

int Functions::GetSlot(std::string spell_name, std::string slot_type) {
	//Execution: 2ms
	int slot = 0; int spellID;
	std::tie(spellID, std::ignore) = GetSpellID(spell_name);
	if (spellID > 0) {
		for (int i = 1; i < 120; i++) {
			if (HasAction(i) && (GetSpellTexture(spellID) == GetActionTexture(i))
				&& ((!IsConsumableAction(i) && slot_type == "SPELL")
				|| (IsConsumableAction(i) && slot_type == "ITEM"))) {
				slot = i;
			}
		}
	}
	return slot;
}

//======================================================================//
//=============================   Units   ==============================//
//======================================================================//

void Functions::TargetUnit(std::string target) {
	LuaCall(("TargetUnit(\"" + target + "\")").c_str());
}

std::string Functions::UnitName(std::string target) {
	LuaCall(("name = UnitName(\"" + target + "\")").c_str());
	std::string name = (char*)GetText("name");
	return name;
}

bool Functions::UnitCanAttack(std::string char1, std::string char2) {
	std::string command = "canAttack = UnitCanAttack(\"" + char1 + "\", \"" + char2 + "\")";
	LuaCall(command.c_str());
	int result = GetIntFromChar((char*)GetText("canAttack"));
	if (result == 1) return true;
	else return false;
}

bool Functions::UnitIsDeadOrGhost(std::string char1) {
	std::string command = "dead = UnitIsDeadOrGhost(\"" + char1 + "\")";
	LuaCall(command.c_str());
	int result = GetIntFromChar((char*)GetText("dead"));
	if (result == 1) return true;
	else return false;
}

bool Functions::CheckInteractDistance(std::string char1, int dist) {
	std::string command = "checkInteract = CheckInteractDistance(\"" + char1 + "\", " + std::to_string(dist) + ")";
	LuaCall(command.c_str());
	int result = GetIntFromChar((char*)GetText("checkInteract"));
	if (result == 1) return true;
	else return false;
}

bool Functions::UnitAffectingCombat(std::string target) {
	std::string command = "cb = UnitAffectingCombat(\"" + target + "\")";
	LuaCall(command.c_str());
	int cb = GetIntFromChar((char*)GetText("cb"));
	if (cb == 1) return true;
	else return false;
}

int Functions::GetPlayerRole() {
	LuaCall("_,_, bonusStat = UnitStat(\"player\", 1)");
	int bonusStr = GetIntFromChar((char*)GetText("bonusStat"));
	LuaCall("_,_, bonusStat = UnitStat(\"player\", 2)");
	int bonusAgi = GetIntFromChar((char*)GetText("bonusStat"));
	LuaCall("_,_, bonusStat = UnitStat(\"player\", 4)");
	int bonusIntel = GetIntFromChar((char*)GetText("bonusStat"));
	if (bonusIntel > bonusStr && bonusIntel > bonusAgi) return 1; //Heal
	else if (IsShieldEquipped()) return 2; //Tank
	else return 3; //Dps
}

std::string Functions::GetTank() {
	for (int i = 1; i <= NumGroupMembers; i++) {
		if (UnitName(tarType + std::to_string(i)) == tankName)
			return tarType + std::to_string(i);
	}
	return "";
}

std::string Functions::UnitClass(std::string target) {
	LuaCall(("class = UnitClass(\"" + target + "\")").c_str());
	std::string tarClass = (char*)GetText("class");
	return tarClass;
}

bool Functions::UnitIsRanged(std::string target) {
	std::string tarClass = UnitClass(target);
	if (tarClass == "Priest" || tarClass == "Warlock" || tarClass == "Mage" || tarClass == "Hunter") return true;
	else if (target == "player" && ((tarClass == "Druid" && GetPlayerRole() == 1)
		|| (tarClass == "Paladin" && GetPlayerRole() == 1) || (tarClass == "Shaman" && GetPlayerRole() < 3))) return true;
	else return false;
}

bool Functions::UnitIsCaster(std::string target) {
	std::string tarClass = UnitClass(target);
	if (tarClass == "Priest" || tarClass == "Warlock" || tarClass == "Mage") return true;
	else if (target == "player" && ((tarClass == "Druid" && GetPlayerRole() == 1)
		|| (tarClass == "Paladin" && GetPlayerRole() == 1) || (tarClass == "Shaman" && GetPlayerRole() < 3))) return true;
	else return false;
}

bool Functions::UnitIsElite(std::string target) {
	LuaCall(("classification = UnitClassification(\"" + target + "\")").c_str());
	std::string classification = (char*)GetText("classification");
	LuaCall(("level = UnitLevel(\"" + target + "\")").c_str());
	int level = GetIntFromChar((char*)GetText("level"));
	LuaCall(("playerCtrl = UnitPlayerControlled(\"" + target + "\")").c_str());
	int playerCtrl = GetIntFromChar((char*)GetText("playerCtrl"));
	if (classification == "elite" || classification == "rareelite" || level == -1 || playerCtrl == 1) return true;
	else return false;
}

std::string Functions::UnitCreatureType(std::string target) {
	LuaCall(("type = UnitCreatureType(\"" + target + "\")").c_str());
	std::string type = (char*)GetText("type");
	return type;
}

bool Functions::IsGroupInCombat() {
	for (int i = 1; i <= NumGroupMembers; i++) {
		if(UnitAffectingCombat(tarType+std::to_string(i))) return true;
	}
	return false;
}

int Functions::GetGroupDead(int mode) {
	if (mode == 0) {
		for (int i = 1; i <= NumGroupMembers; i++) {
			if(UnitIsDeadOrGhost(tarType+std::to_string(i)) && CheckInteractDistance(tarType+std::to_string(i), 4)) return i;
		}
	} else {
		for (int i = NumGroupMembers; i >= 1; i--) {
			if (UnitIsDeadOrGhost(tarType+std::to_string(i)) && CheckInteractDistance(tarType + std::to_string(i), 4)) return i;
		}
	}
	return 0;
}

bool Functions::IsShieldEquipped() {
	LuaCall("_, _, id = string.find(GetInventoryItemLink(\"player\", GetInventorySlotInfo(\"SecondaryHandSlot\")) or \"\", \"(item:%d+:%d+:%d+:%d+)\")");
	int id = GetIntFromChar((char*)GetText("id"));
	if (id > 0) {
		std::string command = "_, _, _, _, itemType = GetItemInfo(" + std::to_string(id) + ")";
		LuaCall(command.c_str());
		std::string itemType = (char*)GetText("itemType");
		if (itemType == "Armor") return true;
	}
	return false;
}

bool Functions::HasWandEquipped() {
	LuaCall("wand = HasWandEquipped()");
	int wand = GetIntFromChar((char*)GetText("wand"));
	if (wand == 1) return true;
	else return false;
}

int Functions::GetComboPoints() {
	LuaCall("pts = GetComboPoints()");
	int pts = GetIntFromChar((char*)GetText("pts"));
	return pts;
}

int Functions::GetTalentInfo(int page, int index) {
	std::string command = "_,_,_,_,pts = GetTalentInfo(" + std::to_string(page) + ", " + std::to_string(index) + ")";
	LuaCall(command.c_str());
	int pts = GetIntFromChar((char*)GetText("pts"));
	return pts;
}

bool Functions::IsCurrentAction(int slot) {
	std::string command = "current = IsCurrentAction(" + std::to_string(slot) + ")";
	LuaCall(command.c_str());
	int current = GetIntFromChar((char*)GetText("current"));
	if (current == 1) return true;
	else return false;
}

float Functions::UnitAttackSpeed(std::string target) {
	std::string command = "aaspeed = UnitAttackSpeed(\"" + target + "\")";
	LuaCall(command.c_str());
	float aaspeed = GetFloatFromChar((char*)GetText("aaspeed"));
	return aaspeed;
}

void Functions::FollowUnit(std::string target) {
	std::string command = "FollowUnit(\"" + target + "\")";
	LuaCall(command.c_str());
}

void Functions::FollowMultibox(std::string unit_name) {
	for (int i = 1; i <= NumGroupMembers; i++) {
		if (UnitName(tarType + std::to_string(i)) == unit_name) {
			if (Functions::CheckInteractDistance(tarType + std::to_string(i), 4)) {
				FollowUnit(tarType + std::to_string(i));
				return;
			}
			else break;
		}
	}
	for (unsigned int y = 0; y < ListUnits.size(); y++) {
		if (ListUnits[y].name == unit_name) {
			float dist = localPlayer->position.DistanceTo(ListUnits[y].position);
			if (dist < 60.0f) {
				localPlayer->ClickToMove(Move, ListUnits[y].Guid, ListUnits[y].position);
			}
			return;
		}
	}
}