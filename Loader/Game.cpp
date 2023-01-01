#include "Game.h"
#include "Functions.h"
#include "Client.h"
#include "MemoryManager.h"
#include "ListAI.h"

#include <iostream>
#include <chrono>

static bool infoB = false;
static std::string playerName = "";

void Game::MainLoop() {
	while (Client::client_running == true) {
		if (!infoB && Functions::GetPlayerGuid() > 0) {
			ThreadSynchronizer::RunOnMainThread(
				[]() {
					playerName = Functions::UnitName("player");
					playerClass = Functions::UnitClass("player");
				}
			);
			std::string msg = ("Name " + playerName + " Class " + playerClass);
			Client::sendMessage(msg);
			infoB = true;
		}
		else if (infoB && Functions::GetPlayerGuid() == 0) {
			infoB = false;
			Client::sendMessage("Name Null Class Null");
		}

		if (keyTarget) {
			ThreadSynchronizer::RunOnMainThread([]() {
				std::string msg = "AssistByName('" + tankName + "')";
				Functions::LuaCall(msg.c_str());
			});
			keyTarget = false;
		}
		else if (keyHearthstone) {
			ThreadSynchronizer::RunOnMainThread([]() { Functions::UseItem("Hearthstone"); });
			keyHearthstone = false;
		}
		else if (keyMount) {
			ThreadSynchronizer::RunOnMainThread([]() { Functions::UseItem("Bridle"); });
			keyMount = false;
		}

		while (Client::bot_running == true) {
			if (Functions::GetPlayerGuid() > 0) { //in-game

				// ========================================== //
				// ===========   Initialisation   =========== //
				// ========================================== //

				auto start = std::chrono::high_resolution_clock::now();

				if (keyTarget) {
					ThreadSynchronizer::RunOnMainThread([]() {
						std::string msg = "AssistByName('" + tankName + "')";
						Functions::LuaCall(msg.c_str());
						});
					keyTarget = false;
				}
				else if (keyHearthstone) {
					ThreadSynchronizer::RunOnMainThread([]() { Functions::UseItem("Hearthstone"); });
					keyHearthstone = false;
				}
				else if (keyMount) {
					ThreadSynchronizer::RunOnMainThread([]() { Functions::UseItem("Bridle"); });
					keyMount = false;
				}

				ThreadSynchronizer::RunOnMainThread(
					[]() {
						if (Functions::IsInRaid()) tarType = "raid";
						else tarType = "party";
						NumGroupMembers = Functions::GetNumGroupMembers();
						IsInGroup = Functions::IsInGroup();

						Functions::EnumerateVisibleObjects(0);
						Functions::ClassifyHeal();

						playerClass = Functions::UnitClass("player");

						if (localPlayer != NULL) targetUnit = localPlayer->getTarget();

						if (Functions::GetRepairAllCost() > 0) Functions::LuaCall("RepairAllItems()");
						if (Functions::GetMerchantNumItems() > 0) Functions::SellUselessItems();
					}
				);
				if (localPlayer != NULL) {

					std::tie(nbrEnemy, nbrCloseEnemy, nbrCloseEnemyFacing, nbrEnemyPlayer) = Functions::countEnemies();

					IsFacing = false;
					if (targetUnit != NULL && targetUnit->attackable && !targetUnit->isdead) IsFacing = localPlayer->isFacing(targetUnit->position, 0.4f);

					distTarget = 0;
					if (targetUnit != NULL) distTarget = localPlayer->position.DistanceTo(targetUnit->position);

					Combat = (localPlayer->flags & UNIT_FLAG_IN_COMBAT) == UNIT_FLAG_IN_COMBAT;

					hasTargetAggro = false;
					if (targetUnit != NULL) {
						for (unsigned int i = 0; i < HasAggro[0].size(); i++) {
							if (targetUnit->Guid == HasAggro[0][i]) hasTargetAggro = true;
						}
					}

					if (!infoB) {
						std::string msg = "Name " + playerName + " Class " + playerClass;
						Client::sendMessage(msg.c_str());
						infoB = true;
					}
				}
				else std::cout << "localPlayer NULL !\n";

				auto end = std::chrono::high_resolution_clock::now();
				std::chrono::duration<double, std::milli> float_ms = end - start;
				std::cout << "Initialisation: elapsed time is " << float_ms.count() << " milliseconds" << std::endl;
				
				// ========================================== //
				// ==============   Movements   ============= //
				// ========================================== //

				start = std::chrono::high_resolution_clock::now();

				if (localPlayer != NULL) {
					float halfPI = acos(0.0);
					Position player_pos = Position(localPlayer->position.X, localPlayer->position.Y, localPlayer->position.Z + 3);
					Position front_pos = Position((cos(localPlayer->facing) * 4) + localPlayer->position.X, (sin(localPlayer->facing) * 4) + localPlayer->position.Y, localPlayer->position.Z + 3);
					Position back_pos = Position((cos(localPlayer->facing + (2 * halfPI)) * 4) + localPlayer->position.X, (sin(localPlayer->facing + (2 * halfPI)) * 4) + localPlayer->position.Y, localPlayer->position.Z + 3);
					ThreadSynchronizer::RunOnMainThread([=]() {
						obstacle_front = ((localPlayer->movement_flags & MOVEFLAG_SWIMMING) != MOVEFLAG_SWIMMING) && Functions::GetDepth(front_pos) > 13;
						obstacle_back = ((localPlayer->movement_flags & MOVEFLAG_SWIMMING) != MOVEFLAG_SWIMMING) && Functions::GetDepth(back_pos) > 13;
						if (targetUnit != NULL) los_target = !Functions::Intersect(player_pos, Position(targetUnit->position.X, targetUnit->position.Y, targetUnit->position.Z + 3));
						hasDrink = Functions::HasDrink();
					});
					bool playerIsRanged = Functions::PlayerIsRanged();
					int drinkingIDs[15] = { 430, 431, 432, 1133, 1135, 1137, 24355, 25696, 26261, 26402, 26473, 26475, 29007, 10250, 22734 };
					int RaptorStrikeIDs[8] = { 2973, 14260, 14261, 14262, 14263, 14264, 14265, 14266 };

					if (IsSitting && ((localPlayer->prctMana > 80) || Combat || !localPlayer->hasBuff(drinkingIDs, 15))) {
						//Stop sitting
						IsSitting = false;
						Functions::pressKey(0x28);
						Functions::releaseKey(0x28);
						Moving = 0;
					}
					else if (!Combat && (localPlayer->speed == 0) && (localPlayer->movement_flags == MOVEFLAG_NONE) && (localPlayer->prctMana < 33) && (hasDrink > 0)) {
						//Drink
						IsSitting = true;
						ThreadSynchronizer::RunOnMainThread([]() { Functions::UseItem(hasDrink); });
					}
					else if (targetUnit != NULL && targetUnit->attackable && !targetUnit->isdead && ((localPlayer->castInfo == 0) || (playerClass == "Hunter" && localPlayer->isCasting(RaptorStrikeIDs, 8))) && (localPlayer->channelInfo == 0)) {
						if (playerIsRanged) {
							if ((Moving == 4 || Moving == 2 || Moving == 5) && (distTarget < 30.0f || obstacle_front)) {
								//Running and (target < 30 yard || obstacle in front) => stop
								Functions::pressKey(0x28);
								Functions::releaseKey(0x28);
								Moving = 0;
							}
							else if (Moving == 6 && (los_target || obstacle_front)) {
								//Looking for LoS, found it => stop
								Functions::pressKey(0x28);
								Functions::releaseKey(0x28);
								Moving = 0;
							}
							else if (!obstacle_back && (Moving == 0 || Moving == 1 || Moving == 4) && (targetUnit->flags & UNIT_FLAG_PLAYER_CONTROLLED)
								&& (targetUnit->flags & UNIT_FLAG_CONFUSED || targetUnit->speed == 0) && distTarget < 12.0f) {
								//Player stunned or rooted and target < 12 yard => Run away
								if (localPlayer->speed == 0) {
									Position oppositeDir = localPlayer->getOppositeDirection(targetUnit->position);
									ThreadSynchronizer::RunOnMainThread([oppositeDir]() { localPlayer->ClickToMove(Move, targetUnit->Guid, oppositeDir); });
								}
								Moving = 1;
							}
							else if ((Moving == 0 || Moving == 6) && !los_target) {
								//Find LoS
								ThreadSynchronizer::RunOnMainThread([=]() {
									for (int i = 0; i < 8; i++) { //Front, left, right...
										if (i != 6) { //We don't want backward
											for (int y = 1; y <= 10; y++) { //Every 3 yards up to 30 check for LoS point
												Position tmp_pos = Position((cos(localPlayer->facing + (i * halfPI / 2) - halfPI) * (y * 3)) + localPlayer->position.X
													, (sin(localPlayer->facing + (i * halfPI / 2) - halfPI) * (y * 3)) + localPlayer->position.Y, localPlayer->position.Z + 3);
												Position tmp_pos2 = Functions::ProjectPos(tmp_pos); tmp_pos2.Z = tmp_pos2.Z + 3;
												bool enemy_close = false;
												for (unsigned int z = 0; z < ListUnits.size(); z++) { //If one enemy (not aggro) is too close, abort
													if ((ListUnits[z].Guid != targetUnit->Guid) && ListUnits[z].attackable && !ListUnits[z].isdead
														&& !(ListUnits[z].flags & UNIT_FLAG_IN_COMBAT) && (ListUnits[z].position.DistanceTo(tmp_pos2) < 5)) {
														enemy_close = true;
													}
												}
												if (enemy_close) break; //Change direction
												else if (!Functions::Intersect(player_pos, tmp_pos2) && (Functions::GetDepth(tmp_pos) < 13)) {
													if (!Functions::Intersect(tmp_pos2, Position(targetUnit->position.X, targetUnit->position.Y, targetUnit->position.Z + 3))) {
														localPlayer->ClickToMove(Move, targetUnit->Guid, tmp_pos2);
														Moving = 6;
														return;
													}
												}
												else break; //There is an obstacle on this path, we need to change
											}
										}
									}
								});
							}
							else if (distTarget > 30.0f && !obstacle_front && !IsSitting && (Moving == 0 || Moving == 2 || Moving == 4)) {
								//Target > 30 yard and no obstacle => Run to it
								ThreadSynchronizer::RunOnMainThread([]() { localPlayer->ClickToMove(Move, targetUnit->Guid, targetUnit->position); });
								Moving = 2;
							}
							else if (Moving == 1 && (obstacle_front || distTarget > 12.0f || targetUnit->speed > 4.5)) {
								//Running away and (obstacle || target > 12 yard || target moving)
								Functions::pressKey(0x28);
								Functions::releaseKey(0x28);
								Moving = 0;
							}
							else if (Moving == 3 && (obstacle_back || distTarget > 12.0f || (!(targetUnit->flags & UNIT_FLAG_PLAYER_CONTROLLED) && hasTargetAggro)
								|| ((targetUnit->flags & UNIT_FLAG_PLAYER_CONTROLLED) && targetUnit->speed >= 4.5))) {
								//Walking backward and (target > 12 yard || Creature aggro || target running)
								Functions::releaseKey(0x28);
								Moving = 0;
							}
							else if ((Moving == 0) && !IsFacing) {
								//Nothing to do: face target
								ThreadSynchronizer::RunOnMainThread([]() { localPlayer->ClickToMove(FaceTarget, targetUnit->Guid, targetUnit->position); });
							}
							else if ((Moving == 0) && distTarget < 12.0f && !obstacle_back && ((!(targetUnit->flags & UNIT_FLAG_PLAYER_CONTROLLED)
								&& !hasTargetAggro && playerClass == "Hunter") || ((targetUnit->flags & UNIT_FLAG_PLAYER_CONTROLLED) && targetUnit->speed <= 4.5))) {
								//((hunter)Creature not aggro || Player slowed) && < 12 yard => Walk backward
								Functions::pressKey(0x28);
								Moving = 3;
							}
						}
						else if(tankName != playerName || tankAutoMove) {
							if (distTarget > 5.0f && !IsSitting && !obstacle_front) {
								ThreadSynchronizer::RunOnMainThread([]() { localPlayer->ClickToMove(Move, targetUnit->Guid, targetUnit->position); });
								Moving = 2;
							}
							else if (Moving == 2 || Moving == 4) {
								Functions::pressKey(0x28);
								Functions::releaseKey(0x28);
								Moving = 0;
							}
							else if (!IsFacing) ThreadSynchronizer::RunOnMainThread([]() { localPlayer->ClickToMove(FaceTarget, targetUnit->Guid, targetUnit->position); });
						}
					}
					else if (Moving == 1 || Moving == 2 || Moving == 6 || (Moving == 4 && obstacle_front)) {
						Functions::pressKey(0x28);
						Functions::releaseKey(0x28);
						Moving = 0;
					}
					else if (Moving == 3) {
						Functions::releaseKey(0x28);
						Moving = 0;
					}
					else if (Moving == 5 && (obstacle_front || targetUnit == NULL || ((targetUnit->unitReaction > Neutral) && (distTarget < 30.0f)))) {
						Functions::pressKey(0x28);
						Functions::releaseKey(0x28);
						Moving = 0;
					}
					else if ((tankName != playerName) && !Combat && !IsSitting && IsInGroup && (localPlayer->castInfo == 0) && (localPlayer->channelInfo == 0) && (targetUnit == NULL || !targetUnit->attackable || targetUnit->isdead)) {
						//Follow
						if(tankName != "null") {
							if (Functions::FollowMultibox(playerIsRanged, positionCircle, 0)) Moving = 4; //(Circle radius, Circle position, Tank or Melee)
						}
						else if (meleeName != "null" && positionCircle != 0) {
							if (Functions::FollowMultibox(playerIsRanged, positionCircle, 1)) Moving = 4;
						}
					}
				}

				end = std::chrono::high_resolution_clock::now();
				float_ms = end - start;
				std::cout << "Movement: elapsed time is " << float_ms.count() << " milliseconds" << std::endl;
				
				// ========================================== //
				// ===============   Actions   ============== //
				// ========================================== //

				start = std::chrono::high_resolution_clock::now();

				if (localPlayer != NULL && !IsSitting) {
					if (playerClass == "Hunter") ListAI::HunterDps();
					else if (playerClass == "Mage") ListAI::MageDps();
					else if (playerClass == "Paladin" && playerSpec == 0) ListAI::PaladinHeal();
					else if (playerClass == "Paladin" && playerSpec == 1) ListAI::PaladinTank();
					else if (playerClass == "Paladin") ListAI::PaladinDps();
					else if (playerClass == "Rogue") ListAI::RogueDps();
				}

				end = std::chrono::high_resolution_clock::now();
				float_ms = end - start;
				std::cout << "Actions: elapsed time is " << float_ms.count() << " milliseconds" << std::endl;
			}
			else if (infoB) {
				infoB = false;
				Client::sendMessage("Name Null Class Null");
			}
			Sleep(250);
		}
		if (Moving > 0) {
			if (Moving < 3) Functions::pressKey(0x28);
			Functions::releaseKey(0x28);
			Moving = 0;
		}
		Sleep(250);
	}
}

int GroupMembersIndex[40];
std::vector<unsigned long long> HasAggro[40];
bool Combat = false, IsSitting = false, bossFight = false, IsInGroup = false, IsFacing = false, hasTargetAggro = false, tankAutoFocus = false, tankAutoMove = false,
	keyTarget = false, keyHearthstone = false, keyMount = false, obstacle_front = false, obstacle_back = false, los_target = false;
int AoEHeal = 0, nbrEnemy = 0, nbrCloseEnemy = 0, nbrCloseEnemyFacing = 0, nbrEnemyPlayer = 0, Moving = 0, NumGroupMembers = 0, playerSpec = 0, positionCircle = 0, hasDrink = 0;
float distTarget = 0;
std::string tarType = "party", playerClass = "null", tankName = "null", meleeName = "null";
std::vector<int> HealTargetArray;
WoWUnit* ccTarget = NULL; WoWUnit* targetUnit = NULL;