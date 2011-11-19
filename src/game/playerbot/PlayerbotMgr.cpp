#include "Config/Config.h"
#include "config.h"
#include "../Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "WorldPacket.h"
#include "../Chat.h"
#include "../ObjectMgr.h"
#include "../GossipDef.h"
#include "../Language.h"
#include "../WaypointMovementGenerator.h"

class LoginQueryHolder;
class CharacterHandler;

Config botConfig;

void PlayerbotMgr::SetInitialWorldSettings()
{
    //Get playerbot configuration file
    if (!botConfig.SetSource(_PLAYERBOT_CONFIG))
        sLog.outError("Playerbot: Unable to open configuration file. Database will be unaccessible. Configuration values will use default.");
    else
        sLog.outString("Playerbot: Using configuration file %s",_PLAYERBOT_CONFIG);

    //Check playerbot config file version
    if (botConfig.GetIntDefault("ConfVersion", 0) != PLAYERBOT_CONF_VERSION)
        sLog.outError("Playerbot: Configuration file version doesn't match expected version. Some config variables may be wrong or missing.");

    if (botConfig.GetBoolDefault("PlayerbotAI.AutobotNamesInUseReset", false))
        PlayerbotMgr::AutobotNamesInUseReset();
}

void PlayerbotMgr::AutobotNamesInUseReset()
{
    // Let's just clean slate this
    CharacterDatabase.DirectExecute("UPDATE playerbot_autobot_names SET in_use = 0");

    // And then set it right again
    CharacterDatabase.DirectExecute("UPDATE playerbot_autobot_names SET playerbot_autobot_names.in_use = 1 WHERE EXISTS (SELECT characters.name FROM characters WHERE playerbot_autobot_names.name = characters.name)");
}

PlayerbotMgr::PlayerbotMgr(Player* const master) : m_master(master)
{
    // load config variables
    m_confMaxNumBots = botConfig.GetIntDefault("PlayerbotAI.MaxNumBots", 9);
    m_confDebugWhisper = botConfig.GetBoolDefault("PlayerbotAI.DebugWhisper", false);
    m_confFollowDistance[0] = botConfig.GetFloatDefault("PlayerbotAI.FollowDistanceMin", 0.5f);
    m_confFollowDistance[1] = botConfig.GetFloatDefault("PlayerbotAI.FollowDistanceMax", 1.0f);
    m_confCollectCombat = botConfig.GetBoolDefault("PlayerbotAI.Collect.Combat", true);
    m_confCollectQuest = botConfig.GetBoolDefault("PlayerbotAI.Collect.Quest", true);
    m_confCollectProfession = botConfig.GetBoolDefault("PlayerbotAI.Collect.Profession", true);
    m_confCollectLoot = botConfig.GetBoolDefault("PlayerbotAI.Collect.Loot", true);
    m_confCollectSkin = botConfig.GetBoolDefault("PlayerbotAI.Collect.Skin", true);
    m_confCollectObjects = botConfig.GetBoolDefault("PlayerbotAI.Collect.Objects", true);
    m_confCollectDistanceMax = botConfig.GetIntDefault("PlayerbotAI.Collect.DistanceMax", 50);
    if (m_confCollectDistanceMax > 100)
    {
        sLog.outError("Playerbot: PlayerbotAI.Collect.DistanceMax higher than allowed. Using 100");
        m_confCollectDistanceMax = 100;
    }
    m_confCollectDistance = botConfig.GetIntDefault("PlayerbotAI.Collect.Distance", 25);
    if (m_confCollectDistance > m_confCollectDistanceMax)
    {
        sLog.outError("Playerbot: PlayerbotAI.Collect.Distance higher than PlayerbotAI.Collect.DistanceMax. Using DistanceMax value");
        m_confCollectDistance = m_confCollectDistanceMax;
    }
}

PlayerbotMgr::~PlayerbotMgr()
{
    LogoutAllBots();
}

void PlayerbotMgr::UpdateAI(const uint32 /*p_time*/) {}

void PlayerbotMgr::HandleMasterIncomingPacket(const WorldPacket& packet)
{
    switch (packet.GetOpcode())
    {

        case CMSG_ACTIVATETAXI:
        {
            WorldPacket p(packet);
            p.rpos(0); // reset reader

            ObjectGuid guid;
            std::vector<uint32> nodes;
            nodes.resize(2);
            uint8 delay = 9;

            p >> guid >> nodes[0] >> nodes[1];

            DEBUG_LOG ("[PlayerbotMgr]: HandleMasterIncomingPacket - Received CMSG_ACTIVATETAXI from %d to %d", nodes[0], nodes[1]);

            for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
            {

                delay = delay + 3;
                Player* const bot = it->second;
                if (!bot)
                    return;

                Group* group = bot->GetGroup();
                if (!group)
                    continue;

                Unit *target = ObjectAccessor::GetUnit(*bot, guid);

                bot->GetPlayerbotAI()->SetIgnoreUpdateTime(delay);

                bot->GetMotionMaster()->Clear(true);
                bot->GetMotionMaster()->MoveFollow(target, INTERACTION_DISTANCE, bot->GetOrientation());
                bot->GetPlayerbotAI()->GetTaxi(guid, nodes);
            }
            return;
        }

        case CMSG_ACTIVATETAXIEXPRESS:
        {
            WorldPacket p(packet);
            p.rpos(0); // reset reader

            ObjectGuid guid;
            uint32 node_count;
            uint8 delay = 9;

            p >> guid >> node_count;

            std::vector<uint32> nodes;

            for (uint32 i = 0; i < node_count; ++i)
            {
                uint32 node;
                p >> node;
                nodes.push_back(node);
            }

            if (nodes.empty())
                return;

            DEBUG_LOG ("[PlayerbotMgr]: HandleMasterIncomingPacket - Received CMSG_ACTIVATETAXIEXPRESS from %d to %d", nodes.front(), nodes.back());

            for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
            {

                delay = delay + 3;
                Player* const bot = it->second;
                if (!bot)
                    return;

                Group* group = bot->GetGroup();
                if (!group)
                    continue;

                Unit *target = ObjectAccessor::GetUnit(*bot, guid);

                bot->GetPlayerbotAI()->SetIgnoreUpdateTime(delay);

                bot->GetMotionMaster()->Clear(true);
                bot->GetMotionMaster()->MoveFollow(target, INTERACTION_DISTANCE, bot->GetOrientation());
                bot->GetPlayerbotAI()->GetTaxi(guid, nodes);
            }
            return;
        }

        case CMSG_MOVE_SPLINE_DONE:
        {
            DEBUG_LOG ("[PlayerbotMgr]: HandleMasterIncomingPacket - Received CMSG_MOVE_SPLINE_DONE");

            WorldPacket p(packet);
            p.rpos(0); // reset reader

            ObjectGuid guid;                                        // used only for proper packet read
            MovementInfo movementInfo;                              // used only for proper packet read

            p >> guid.ReadAsPacked();
            p >> movementInfo;
            p >> Unused<uint32>();                          // unk

            for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
            {

                Player* const bot = it->second;
                if (!bot)
                    return;

                // in taxi flight packet received in 2 case:
                // 1) end taxi path in far (multi-node) flight
                // 2) switch from one map to other in case multi-map taxi path
                // we need process only (1)
                uint32 curDest = bot->m_taxi.GetTaxiDestination();
                if (!curDest)
                    return;

                TaxiNodesEntry const* curDestNode = sTaxiNodesStore.LookupEntry(curDest);

                // far teleport case
                if (curDestNode && curDestNode->map_id != bot->GetMapId())
                {
                    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == FLIGHT_MOTION_TYPE)
                    {
                        // short preparations to continue flight
                        FlightPathMovementGenerator* flight = (FlightPathMovementGenerator *) (bot->GetMotionMaster()->top());

                        flight->Interrupt(*bot);                // will reset at map landing

                        flight->SetCurrentNodeAfterTeleport();
                        TaxiPathNodeEntry const& node = flight->GetPath()[flight->GetCurrentNode()];
                        flight->SkipCurrentNode();

                        bot->TeleportTo(curDestNode->map_id, node.x, node.y, node.z, bot->GetOrientation());
                    }
                    return;
                }

                uint32 destinationnode = bot->m_taxi.NextTaxiDestination();
                if (destinationnode > 0)                                // if more destinations to go
                {
                    // current source node for next destination
                    uint32 sourcenode = bot->m_taxi.GetTaxiSource();

                    // Add to taximask middle hubs in taxicheat mode (to prevent having player with disabled taxicheat and not having back flight path)
                    if (bot->isTaxiCheater())
                        if (bot->m_taxi.SetTaximaskNode(sourcenode))
                        {
                            WorldPacket data(SMSG_NEW_TAXI_PATH, 0);
                            bot->GetSession()->SendPacket(&data);
                        }

                    DEBUG_LOG ("[PlayerbotMgr]: HandleMasterIncomingPacket - Received CMSG_MOVE_SPLINE_DONE Taxi has to go from %u to %u", sourcenode, destinationnode);

                    uint32 mountDisplayId = sObjectMgr.GetTaxiMountDisplayId(sourcenode, bot->GetTeam());

                    uint32 path, cost;
                    sObjectMgr.GetTaxiPath(sourcenode, destinationnode, path, cost);

                    if (path && mountDisplayId)
                        bot->GetSession()->SendDoFlight(mountDisplayId, path, 1);          // skip start fly node
                    else
                        bot->m_taxi.ClearTaxiDestinations();    // clear problematic path and next
                }
                else
                    /* std::ostringstream out;
                       out << "Destination reached" << bot->GetName();
                       ChatHandler ch(m_master);
                       ch.SendSysMessage(out.str().c_str()); */
                    bot->m_taxi.ClearTaxiDestinations();        // Destination, clear source node
            }
            return;
        }

        // if master is logging out, log out all bots
        case CMSG_LOGOUT_REQUEST:
        {
            LogoutAllBots();
            return;
        }

        // If master inspects one of his bots, give the master useful info in chat window
        // such as inventory that can be equipped
        case CMSG_INSPECT:
        {
            WorldPacket p(packet);
            p.rpos(0); // reset reader
            ObjectGuid guid;
            p >> guid;
            Player* const bot = GetPlayerBot(guid);
            if (bot) bot->GetPlayerbotAI()->SendNotEquipList(*bot);
            return;
        }

        // handle emotes from the master
        //case CMSG_EMOTE:
        case CMSG_TEXT_EMOTE:
        {
            WorldPacket p(packet);
            p.rpos(0); // reset reader
            uint32 emoteNum;
            p >> emoteNum;

            /* std::ostringstream out;
               out << "emote is: " << emoteNum;
               ChatHandler ch(m_master);
               ch.SendSysMessage(out.str().c_str()); */

            switch (emoteNum)
            {
                case TEXTEMOTE_BOW:
                {
                    // Buff anyone who bows before me. Useful for players not in bot's group
                    // How do I get correct target???
                    //Player* const pPlayer = GetPlayerBot(m_master->GetSelection());
                    //if (pPlayer->GetPlayerbotAI()->GetClassAI())
                    //    pPlayer->GetPlayerbotAI()->GetClassAI()->BuffPlayer(pPlayer);
                    return;
                }
                /*
                   case TEXTEMOTE_BONK:
                   {
                    Player* const pPlayer = GetPlayerBot(m_master->GetSelection());
                    if (!pPlayer || !pPlayer->GetPlayerbotAI())
                        return;
                    PlayerbotAI* const pBot = pPlayer->GetPlayerbotAI();

                    ChatHandler ch(m_master);
                    {
                        std::ostringstream out;
                        out << "time(0): " << time(0)
                            << " m_ignoreAIUpdatesUntilTime: " << pBot->m_ignoreAIUpdatesUntilTime;
                        ch.SendSysMessage(out.str().c_str());
                    }
                    {
                        std::ostringstream out;
                        out << "m_TimeDoneEating: " << pBot->m_TimeDoneEating
                            << " m_TimeDoneDrinking: " << pBot->m_TimeDoneDrinking;
                        ch.SendSysMessage(out.str().c_str());
                    }
                    {
                        std::ostringstream out;
                        out << "m_CurrentlyCastingSpellId: " << pBot->m_CurrentlyCastingSpellId;
                        ch.SendSysMessage(out.str().c_str());
                    }
                    {
                        std::ostringstream out;
                        out << "IsBeingTeleported() " << pBot->GetPlayer()->IsBeingTeleported();
                        ch.SendSysMessage(out.str().c_str());
                    }
                    {
                        std::ostringstream out;
                        bool tradeActive = (pBot->GetPlayer()->GetTrader()) ? true : false;
                        out << "tradeActive: " << tradeActive;
                        ch.SendSysMessage(out.str().c_str());
                    }
                    {
                        std::ostringstream out;
                        out << "IsCharmed() " << pBot->getPlayer()->isCharmed();
                        ch.SendSysMessage(out.str().c_str());
                    }
                    return;
                   }
                 */

                case TEXTEMOTE_EAT:
                case TEXTEMOTE_DRINK:
                {
                    for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
                    {
                        Player* const bot = it->second;
                        bot->GetPlayerbotAI()->Feast();
                    }
                    return;
                }

                // emote to attack selected target
                case TEXTEMOTE_POINT:
                {
                    ObjectGuid attackOnGuid = m_master->GetSelectionGuid();
                    if (!attackOnGuid)
                        return;

                    Unit* thingToAttack = ObjectAccessor::GetUnit(*m_master, attackOnGuid);
                    if (!thingToAttack) return;

                    Player *bot = 0;
                    for (PlayerBotMap::iterator itr = m_playerBots.begin(); itr != m_playerBots.end(); ++itr)
                    {
                        bot = itr->second;
                        if (!bot->IsFriendlyTo(thingToAttack) && bot->IsWithinLOSInMap(thingToAttack))
                            bot->GetPlayerbotAI()->GetCombatTarget(thingToAttack);
                    }
                    return;
                }

                // emote to stay
                case TEXTEMOTE_STAND:
                {
                    Player* const bot = GetPlayerBot(m_master->GetSelectionGuid());
                    if (bot)
                        bot->GetPlayerbotAI()->SetMovementOrder(PlayerbotAI::MOVEMENT_STAY);
                    else
                        for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
                        {
                            Player* const bot = it->second;
                            bot->GetPlayerbotAI()->SetMovementOrder(PlayerbotAI::MOVEMENT_STAY);
                        }
                    return;
                }

                // 324 is the followme emote (not defined in enum)
                // if master has bot selected then only bot follows, else all bots follow
                case 324:
                case TEXTEMOTE_WAVE:
                {
                    Player* const bot = GetPlayerBot(m_master->GetSelectionGuid());
                    if (bot)
                        bot->GetPlayerbotAI()->SetMovementOrder(PlayerbotAI::MOVEMENT_FOLLOW, m_master);
                    else
                        for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
                        {
                            Player* const bot = it->second;
                            bot->GetPlayerbotAI()->SetMovementOrder(PlayerbotAI::MOVEMENT_FOLLOW, m_master);
                        }
                    return;
                }
            }
            return;
        } /* EMOTE ends here */

        case CMSG_GAMEOBJ_USE: // not sure if we still need this one
        case CMSG_GAMEOBJ_REPORT_USE:
        {
            WorldPacket p(packet);
            p.rpos(0);     // reset reader
            ObjectGuid objGUID;
            p >> objGUID;

            for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
            {
                Player* const bot = it->second;

                GameObject *obj = m_master->GetMap()->GetGameObject(objGUID);
                if (!obj)
                    return;

                // add other go types here, i.e.:
                // GAMEOBJECT_TYPE_CHEST - loot quest items of chest
                if (obj->GetGoType() == GAMEOBJECT_TYPE_QUESTGIVER)
                {
                    bot->GetPlayerbotAI()->TurnInQuests(obj);

                    // auto accept every available quest this NPC has
                    bot->PrepareQuestMenu(objGUID);
                    QuestMenu& questMenu = bot->PlayerTalkClass->GetQuestMenu();
                    for (uint32 iI = 0; iI < questMenu.MenuItemCount(); ++iI)
                    {
                        QuestMenuItem const& qItem = questMenu.GetItem(iI);
                        uint32 questID = qItem.m_qId;
                        if (!bot->GetPlayerbotAI()->AddQuest(questID, obj))
                            DEBUG_LOG("Couldn't take quest");
                    }
                }
            }
        }
        break;

        case CMSG_QUESTGIVER_HELLO:
        {
            WorldPacket p(packet);
            p.rpos(0);    // reset reader
            ObjectGuid npcGUID;
            p >> npcGUID;

            WorldObject* pNpc = m_master->GetMap()->GetWorldObject(npcGUID);
            if (!pNpc)
                return;

            // for all master's bots
            for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
            {
                Player* const bot = it->second;
                bot->GetPlayerbotAI()->TurnInQuests(pNpc);
            }

            return;
        }

        // if master accepts a quest, bots should also try to accept quest
        case CMSG_QUESTGIVER_ACCEPT_QUEST:
        {
            WorldPacket p(packet);
            p.rpos(0);    // reset reader
            ObjectGuid guid;
            uint32 quest;
            uint32 unk1;
            p >> guid >> quest >> unk1;

            DEBUG_LOG ("[PlayerbotMgr]: HandleMasterIncomingPacket - Received CMSG_QUESTGIVER_ACCEPT_QUEST npc = %s, quest = %u, unk1 = %u", guid.GetString().c_str(), quest, unk1);

            Quest const* qInfo = sObjectMgr.GetQuestTemplate(quest);
            if (qInfo)
                for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
                {
                    Player* const bot = it->second;

                    if (bot->GetQuestStatus(quest) == QUEST_STATUS_COMPLETE)
                        bot->GetPlayerbotAI()->TellMaster("I already completed that quest.");
                    else if (!bot->CanTakeQuest(qInfo, false))
                    {
                        if (!bot->SatisfyQuestStatus(qInfo, false))
                            bot->GetPlayerbotAI()->TellMaster("I already have that quest.");
                        else
                            bot->GetPlayerbotAI()->TellMaster("I can't take that quest.");
                    }
                    else if (!bot->SatisfyQuestLog(false))
                        bot->GetPlayerbotAI()->TellMaster("My quest log is full.");
                    else if (!bot->CanAddQuest(qInfo, false))
                        bot->GetPlayerbotAI()->TellMaster("I can't take that quest because it requires that I take items, but my bags are full!");

                    else
                    {
                        p.rpos(0);         // reset reader
                        bot->GetSession()->HandleQuestgiverAcceptQuestOpcode(p);
                        bot->GetPlayerbotAI()->TellMaster("Got the quest.");

                        // build needed items if quest contains any
                        for (int i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; i++)
                            if (qInfo->ReqItemCount[i] > 0)
                            {
                                bot->GetPlayerbotAI()->SetQuestNeedItems();
                                break;
                            }

                        // build needed creatures if quest contains any
                        for (int i = 0; i < QUEST_OBJECTIVES_COUNT; i++)
                            if (qInfo->ReqCreatureOrGOCount[i] > 0)
                            {
                                bot->GetPlayerbotAI()->SetQuestNeedCreatures();
                                break;
                            }
                    }
                }
            return;
        }

        case CMSG_AREATRIGGER:
        {
            WorldPacket p(packet);

            for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
            {
                Player* const bot = it->second;

                p.rpos(0);         // reset reader
                bot->GetSession()->HandleAreaTriggerOpcode(p);
            }
            return;
        }

        case CMSG_QUESTGIVER_COMPLETE_QUEST:
        {
            WorldPacket p(packet);
            p.rpos(0);    // reset reader
            uint32 quest;
            ObjectGuid npcGUID;
            p >> npcGUID >> quest;

            DEBUG_LOG ("[PlayerbotMgr]: HandleMasterIncomingPacket - Received CMSG_QUESTGIVER_COMPLETE_QUEST npc = %s, quest = %u", npcGUID.GetString().c_str(), quest);

            WorldObject* pNpc = m_master->GetMap()->GetWorldObject(npcGUID);
            if (!pNpc)
                return;

            // for all master's bots
            for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
            {
                Player* const bot = it->second;
                bot->GetPlayerbotAI()->TurnInQuests(pNpc);
            }
            return;
        }

        case CMSG_LOOT_ROLL:
        {

            WorldPacket p(packet);    //WorldPacket packet for CMSG_LOOT_ROLL, (8+4+1)
            ObjectGuid Guid;
            uint32 NumberOfPlayers;
            uint8 rollType;
            p.rpos(0);    //reset packet pointer
            p >> Guid;    //guid of the item rolled
            p >> NumberOfPlayers;    //number of players invited to roll
            p >> rollType;    //need,greed or pass on roll

            for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
            {

                uint32 choice;

                Player* const bot = it->second;
                if (!bot)
                    return;

                Group* group = bot->GetGroup();
                if (!group)
                    return;

                (bot->GetPlayerbotAI()->CanStore()) ? choice = urand(0, 3) : choice = 0;  // pass = 0, need = 1, greed = 2, disenchant = 3

                group->CountRollVote(bot, Guid, NumberOfPlayers, RollVote(choice));

                switch (choice)
                {
                    case ROLL_NEED:
                        bot->GetAchievementMgr().UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_ROLL_NEED, 1);
                        break;
                    case ROLL_GREED:
                        bot->GetAchievementMgr().UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_ROLL_GREED, 1);
                        break;
                }
            }
            return;
        }

        // Handle GOSSIP activate actions, prior to GOSSIP select menu actions
        case CMSG_GOSSIP_HELLO:
        {
            DEBUG_LOG ("[PlayerbotMgr]: HandleMasterIncomingPacket - Received CMSG_GOSSIP_HELLO");

            WorldPacket p(packet);    //WorldPacket packet for CMSG_GOSSIP_HELLO, (8)
            ObjectGuid guid;
            p.rpos(0);                //reset packet pointer
            p >> guid;

            for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
            {
                Player* const bot = it->second;
                if (!bot)
                    return;

                Creature *pCreature = bot->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_NONE);
                if (!pCreature)
                {
                    DEBUG_LOG ("[PlayerbotMgr]: HandleMasterIncomingPacket - Received  CMSG_GOSSIP_HELLO %s not found or you can't interact with him.", guid.GetString().c_str());
                    return;
                }

                GossipMenuItemsMapBounds pMenuItemBounds = sObjectMgr.GetGossipMenuItemsMapBounds(pCreature->GetCreatureInfo()->GossipMenuId);
                for (GossipMenuItemsMap::const_iterator itr = pMenuItemBounds.first; itr != pMenuItemBounds.second; ++itr)
                {
                    uint32 npcflags = pCreature->GetUInt32Value(UNIT_NPC_FLAGS);

                    if (!(itr->second.npc_option_npcflag & npcflags))
                        continue;

                    switch (itr->second.option_id)
                    {
                        case GOSSIP_OPTION_TAXIVENDOR:
                        {
                            // bot->GetPlayerbotAI()->TellMaster("PlayerbotMgr:GOSSIP_OPTION_TAXIVENDOR");
                            bot->GetSession()->SendLearnNewTaxiNode(pCreature);
                            break;
                        }
                        case GOSSIP_OPTION_QUESTGIVER:
                        {
                            // bot->GetPlayerbotAI()->TellMaster("PlayerbotMgr:GOSSIP_OPTION_QUESTGIVER");
                            bot->GetPlayerbotAI()->TurnInQuests(pCreature);
                            break;
                        }
                        case GOSSIP_OPTION_VENDOR:
                        {
                            // bot->GetPlayerbotAI()->TellMaster("PlayerbotMgr:GOSSIP_OPTION_VENDOR");
                            break;
                        }
                        case GOSSIP_OPTION_STABLEPET:
                        {
                            // bot->GetPlayerbotAI()->TellMaster("PlayerbotMgr:GOSSIP_OPTION_STABLEPET");
                            break;
                        }
                        case GOSSIP_OPTION_AUCTIONEER:
                        {
                            // bot->GetPlayerbotAI()->TellMaster("PlayerbotMgr:GOSSIP_OPTION_AUCTIONEER");
                            break;
                        }
                        case GOSSIP_OPTION_BANKER:
                        {
                            // bot->GetPlayerbotAI()->TellMaster("PlayerbotMgr:GOSSIP_OPTION_BANKER");
                            break;
                        }
                        case GOSSIP_OPTION_INNKEEPER:
                        {
                            // bot->GetPlayerbotAI()->TellMaster("PlayerbotMgr:GOSSIP_OPTION_INNKEEPER");
                            break;
                        }
                    }
                }
            }
            return;
        }

        case CMSG_SPIRIT_HEALER_ACTIVATE:
        {
            // DEBUG_LOG ("[PlayerbotMgr]: HandleMasterIncomingPacket - Received CMSG_SPIRIT_HEALER_ACTIVATE SpiritHealer is resurrecting the Player %s",m_master->GetName());
            for (PlayerBotMap::iterator itr = m_playerBots.begin(); itr != m_playerBots.end(); ++itr)
            {
                Player* const bot = itr->second;
                Group *grp = bot->GetGroup();
                if (grp)
                    grp->RemoveMember(bot->GetObjectGuid(), 1);
            }
            return;
        }

        case CMSG_LIST_INVENTORY:
        {
            if (!botConfig.GetBoolDefault("PlayerbotAI.SellGarbage", true))
                return;

            WorldPacket p(packet);
            p.rpos(0);  // reset reader
            ObjectGuid npcGUID;
            p >> npcGUID;

            Object* const pNpc = (WorldObject *) m_master->GetObjectByTypeMask(npcGUID, TYPEMASK_CREATURE_OR_GAMEOBJECT);
            if (!pNpc)
                return;

            // for all master's bots
            for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
            {
                Player* const bot = it->second;
                if (!bot->IsInMap(static_cast<WorldObject *>(pNpc)))
                {
                    bot->GetPlayerbotAI()->TellMaster("I'm too far away to sell items!");
                    continue;
                }
                else
                    bot->GetPlayerbotAI()->SellGarbage();
            }
            return;
        }

            /*
               case CMSG_NAME_QUERY:
               case MSG_MOVE_START_FORWARD:
               case MSG_MOVE_STOP:
               case MSG_MOVE_SET_FACING:
               case MSG_MOVE_START_STRAFE_LEFT:
               case MSG_MOVE_START_STRAFE_RIGHT:
               case MSG_MOVE_STOP_STRAFE:
               case MSG_MOVE_START_BACKWARD:
               case MSG_MOVE_HEARTBEAT:
               case CMSG_STANDSTATECHANGE:
               case CMSG_QUERY_TIME:
               case CMSG_CREATURE_QUERY:
               case CMSG_GAMEOBJECT_QUERY:
               case MSG_MOVE_JUMP:
               case MSG_MOVE_FALL_LAND:
                return;

               default:
               {
                const char* oc = LookupOpcodeName(packet.GetOpcode());
                // ChatHandler ch(m_master);
                // ch.SendSysMessage(oc);

                std::ostringstream out;
                out << "masterin: " << oc;
                sLog.outError(out.str().c_str());
               }
             */
    }
}

void PlayerbotMgr::HandleMasterOutgoingPacket(const WorldPacket& /*packet*/)
{
    /*
       switch (packet.GetOpcode())
       {
        // maybe our bots should only start looting after the master loots?
        //case SMSG_LOOT_RELEASE_RESPONSE: {}
        case SMSG_NAME_QUERY_RESPONSE:
        case SMSG_MONSTER_MOVE:
        case SMSG_COMPRESSED_UPDATE_OBJECT:
        case SMSG_DESTROY_OBJECT:
        case SMSG_UPDATE_OBJECT:
        case SMSG_STANDSTATE_UPDATE:
        case MSG_MOVE_HEARTBEAT:
        case SMSG_QUERY_TIME_RESPONSE:
        case SMSG_AURA_UPDATE_ALL:
        case SMSG_CREATURE_QUERY_RESPONSE:
        case SMSG_GAMEOBJECT_QUERY_RESPONSE:
            return;
        default:
        {
            const char* oc = LookupOpcodeName(packet.GetOpcode());

            std::ostringstream out;
            out << "masterout: " << oc;
            sLog.outError(out.str().c_str());
        }
       }
     */
}

void PlayerbotMgr::LogoutAllBots()
{
    while (true)
    {
        PlayerBotMap::const_iterator itr = GetPlayerBotsBegin();
        if (itr == GetPlayerBotsEnd()) break;
        Player* bot = itr->second;
        LogoutPlayerBot(bot->GetObjectGuid());
    }
    RemoveAllBotsFromGroup();
}

void PlayerbotMgr::Stay()
{
    for (PlayerBotMap::const_iterator itr = GetPlayerBotsBegin(); itr != GetPlayerBotsEnd(); ++itr)
    {
        Player* bot = itr->second;
        bot->GetMotionMaster()->Clear();
    }
}

// Playerbot mod: logs out a Playerbot.
void PlayerbotMgr::LogoutPlayerBot(ObjectGuid guid)
{
    Player* bot = GetPlayerBot(guid);
    if (bot)
    {
        WorldSession * botWorldSessionPtr = bot->GetSession();
        m_playerBots.erase(guid);    // deletes bot player ptr inside this WorldSession PlayerBotMap
        botWorldSessionPtr->LogoutPlayer(true); // this will delete the bot Player object and PlayerbotAI object
        delete botWorldSessionPtr;  // finally delete the bot's WorldSession
    }
}

// Playerbot mod: Gets a player bot Player object for this WorldSession master
Player* PlayerbotMgr::GetPlayerBot(ObjectGuid playerGuid) const
{
    PlayerBotMap::const_iterator it = m_playerBots.find(playerGuid);
    return (it == m_playerBots.end()) ? 0 : it->second;
}

void PlayerbotMgr::OnBotLogin(Player * const bot)
{
    // give the bot some AI, object is owned by the player class
    PlayerbotAI* ai = new PlayerbotAI(this, bot);
    bot->SetPlayerbotAI(ai);

    // tell the world session that they now manage this new bot
    m_playerBots[bot->GetObjectGuid()] = bot;

    // if bot is in a group and master is not in group then
    // have bot leave their group
    if (bot->GetGroup() &&
        (m_master->GetGroup() == NULL ||
         m_master->GetGroup()->IsMember(bot->GetObjectGuid()) == false))
        bot->RemoveFromGroup();

    // sometimes master can lose leadership, pass leadership to master check
    const ObjectGuid masterGuid = m_master->GetObjectGuid();
    if (m_master->GetGroup() &&
        !m_master->GetGroup()->IsLeader(masterGuid))
        {
                // But only do so if one of the master's bots is leader
                for (PlayerBotMap::const_iterator itr = GetPlayerBotsBegin(); itr != GetPlayerBotsEnd(); itr++)
                {
                        Player* bot = itr->second;
                        if ( m_master->GetGroup()->IsLeader(bot->GetObjectGuid()) )
                        {
                                m_master->GetGroup()->ChangeLeader(masterGuid);
                                break;
                        }
                }
        }
}

void PlayerbotMgr::RemoveAllBotsFromGroup()
{
    for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); m_master->GetGroup() && it != GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        if (bot->IsInSameGroupWith(m_master))
            m_master->GetGroup()->RemoveMember(bot->GetObjectGuid(), 0);
    }
}

void Creature::LoadBotMenu(Player *pPlayer)
{

    if (pPlayer->GetPlayerbotAI()) return;
    ObjectGuid guid = pPlayer->GetObjectGuid();
    uint32 accountId = sObjectMgr.GetPlayerAccountIdByGUID(guid);
    QueryResult *result = CharacterDatabase.PQuery("SELECT guid, name FROM characters WHERE account='%d'", accountId);
    do
    {
        Field *fields = result->Fetch();
        ObjectGuid guidlo = ObjectGuid(fields[0].GetUInt64());
        std::string name = fields[1].GetString();
        std::string word = "";

        if ((guid == ObjectGuid()) || (guid == guidlo))
        {
            //not found or himself
        }
        else
        {
            // if(sConfig.GetBoolDefault("PlayerbotAI.DisableBots", false)) return;
            // create the manager if it doesn't already exist
            if (!pPlayer->GetPlayerbotMgr())
                pPlayer->SetPlayerbotMgr(new PlayerbotMgr(pPlayer));
            if (pPlayer->GetPlayerbotMgr()->GetPlayerBot(guidlo) == NULL) // add (if not already in game)
            {
                word += "Recruit ";
                word += name;
                word += " as a Bot.";
                pPlayer->PlayerTalkClass->GetGossipMenu().AddMenuItem((uint8) 9, word, guidlo, GOSSIP_OPTION_BOT, word, false);
            }
            else if (pPlayer->GetPlayerbotMgr()->GetPlayerBot(guidlo) != NULL) // remove (if in game)
            {
                word += "Dismiss ";
                word += name;
                word += " from duty.";
                pPlayer->PlayerTalkClass->GetGossipMenu().AddMenuItem((uint8) 0, word, guidlo, GOSSIP_OPTION_BOT, word, false);
            }
        }
    }
    while (result->NextRow());
    delete result;
}

void Player::skill(std::list<uint32>& m_spellsToLearn)
{
    for (SkillStatusMap::const_iterator itr = mSkillStatus.begin(); itr != mSkillStatus.end(); ++itr)
    {
        if (itr->second.uState == SKILL_DELETED)
            continue;

        uint32 pskill = itr->first;

        m_spellsToLearn.push_back(pskill);
    }
}

void Player::MakeTalentGlyphLink(std::ostringstream &out)
{
    // |cff4e96f7|Htalent:1396:4|h[Unleashed Fury]|h|r
    // |cff66bbff|Hglyph:23:460|h[Glyph of Fortitude]|h|r

    if (m_specsCount)
        // loop through all specs (only 1 for now)
        for (uint32 specIdx = 0; specIdx < m_specsCount; ++specIdx)
        {
            // find class talent tabs (all players have 3 talent tabs)
            uint32 const* talentTabIds = GetTalentTabPages(getClass());

            out << "\n" << "Active Talents ";

            for (uint32 i = 0; i < 3; ++i)
            {
                uint32 talentTabId = talentTabIds[i];
                for (PlayerTalentMap::iterator iter = m_talents[specIdx].begin(); iter != m_talents[specIdx].end(); ++iter)
                {
                    PlayerTalent talent = (*iter).second;

                    if (talent.state == PLAYERSPELL_REMOVED)
                        continue;

                    // skip another tab talents
                    if (talent.talentEntry->TalentTab != talentTabId)
                        continue;

                    TalentEntry const* talentInfo = sTalentStore.LookupEntry(talent.talentEntry->TalentID);

                    SpellEntry const* spell_entry = sSpellStore.LookupEntry(talentInfo->RankID[talent.currentRank]);

                    out << "|cff4e96f7|Htalent:" << talent.talentEntry->TalentID << ":" << talent.currentRank
                        << " |h[" << spell_entry->SpellName[GetSession()->GetSessionDbcLocale()] << "]|h|r";
                }
            }

            uint32 freepoints = 0;

            out << " Unspent points : ";

            if ((freepoints = GetFreeTalentPoints()) > 0)
                out << "|h|cff00ff00" << freepoints << "|h|r";
            else
                out << "|h|cffff0000" << freepoints << "|h|r";

            out << "\n" << "Active Glyphs ";
            // GlyphProperties.dbc
            for (uint8 i = 0; i < MAX_GLYPH_SLOT_INDEX; ++i)
            {
                GlyphPropertiesEntry const* glyph = sGlyphPropertiesStore.LookupEntry(m_glyphs[specIdx][i].GetId());
                if (!glyph)
                    continue;

                SpellEntry const* spell_entry = sSpellStore.LookupEntry(glyph->SpellId);

                out << "|cff66bbff|Hglyph:" << GetGlyphSlot(i) << ":" << m_glyphs[specIdx][i].GetId()
                    << " |h[" << spell_entry->SpellName[GetSession()->GetSessionDbcLocale()] << "]|h|r";

            }
        }
}

void Player::chompAndTrim(std::string& str)
{
    while (str.length() > 0)
    {
        char lc = str[str.length() - 1];
        if (lc == '\r' || lc == '\n' || lc == ' ' || lc == '"' || lc == '\'')
            str = str.substr(0, str.length() - 1);
        else
            break;
    }

	while (str.length() > 0)
	{
		char lc = str[0];
		if (lc == ' ' || lc == '"' || lc == '\'')
			str = str.substr(1, str.length() - 1);
		else
			break;
	}
}

bool Player::getNextQuestId(const std::string& pString, unsigned int& pStartPos, unsigned int& pId)
{
    bool result = false;
    unsigned int i;
    for (i = pStartPos; i < pString.size(); ++i)
    {
        if (pString[i] == ',')
            break;
    }
    if (i > pStartPos)
    {
        std::string idString = pString.substr(pStartPos, i - pStartPos);
        pStartPos = i + 1;
        chompAndTrim(idString);
        pId = atoi(idString.c_str());
        result = true;
    }
    return(result);
}

bool Player::requiredQuests(const char* pQuestIdString)
{
    if (pQuestIdString != NULL)
    {
        unsigned int pos = 0;
        unsigned int id;
        std::string confString(pQuestIdString);
        chompAndTrim(confString);
        while (getNextQuestId(confString, pos, id))
        {
            QuestStatus status = GetQuestStatus(id);
            if (status == QUEST_STATUS_COMPLETE)
                return true;
        }
    }
    return false;
}

bool ChatHandler::HandlePlayerbotCommand(char* args)
{
    if (!(m_session->GetSecurity() > SEC_PLAYER))
        if (botConfig.GetBoolDefault("PlayerbotAI.DisableBots", false))
        {
            PSendSysMessage("|cffff0000Playerbot system is currently disabled!");
            SetSentErrorMessage(true);
            return false;
        }

    if (!m_session)
    {
        PSendSysMessage("|cffff0000You may only add bots from an active session");
        SetSentErrorMessage(true);
        return false;
    }

    if (!*args)
    {
        PSendSysMessage("|cffff0000usage: add PLAYERNAME  or  remove PLAYERNAME");
        SetSentErrorMessage(true);
        return false;
    }

    char *cmd = strtok ((char *) args, " ");
    char *charname = strtok (NULL, " ");
    if (!cmd || !charname)
    {
        PSendSysMessage("|cffff0000usage: add PLAYERNAME  or  remove PLAYERNAME");
        SetSentErrorMessage(true);
        return false;
    }

    std::string cmdStr = cmd;
    std::string charnameStr = charname;

    if (!normalizePlayerName(charnameStr))
        return false;

    ObjectGuid guid = sObjectMgr.GetPlayerGuidByName(charnameStr.c_str());
    if (guid == ObjectGuid() || (guid == m_session->GetPlayer()->GetObjectGuid()))
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    uint32 accountId = sObjectMgr.GetPlayerAccountIdByGUID(guid);
    if (accountId != m_session->GetAccountId())
    {
        PSendSysMessage("|cffff0000You may only add bots from the same account.");
        SetSentErrorMessage(true);
        return false;
    }

    // create the playerbot manager if it doesn't already exist
    PlayerbotMgr* mgr = m_session->GetPlayer()->GetPlayerbotMgr();
    if (!mgr)
    {
        mgr = new PlayerbotMgr(m_session->GetPlayer());
        m_session->GetPlayer()->SetPlayerbotMgr(mgr);
    }

    QueryResult *resultchar = CharacterDatabase.PQuery("SELECT COUNT(*) FROM characters WHERE online = '1' AND account = '%u'", m_session->GetAccountId());
    if (resultchar)
    {
        Field *fields = resultchar->Fetch();
        int acctcharcount = fields[0].GetUInt32();
        int maxnum = botConfig.GetIntDefault("PlayerbotAI.MaxNumBots", 9);
        if (!(m_session->GetSecurity() > SEC_PLAYER))
            if (acctcharcount > maxnum && (cmdStr == "add" || cmdStr == "login"))
            {
                PSendSysMessage("|cffff0000You cannot summon anymore bots.(Current Max: |cffffffff%u)", maxnum);
                SetSentErrorMessage(true);
                delete resultchar;
                return false;
            }
        delete resultchar;
    }

    QueryResult *resultlvl = CharacterDatabase.PQuery("SELECT level,name FROM characters WHERE guid = '%u'", guid.GetCounter());
    if (resultlvl)
    {
        Field *fields = resultlvl->Fetch();
        int charlvl = fields[0].GetUInt32();
        int maxlvl = botConfig.GetIntDefault("PlayerbotAI.RestrictBotLevel", 80);
        if (!(m_session->GetSecurity() > SEC_PLAYER))
            if (charlvl > maxlvl)
            {
                PSendSysMessage("|cffff0000You cannot summon |cffffffff[%s]|cffff0000, it's level is too high.(Current Max:lvl |cffffffff%u)", fields[1].GetString(), maxlvl);
                SetSentErrorMessage(true);
                delete resultlvl;
                return false;
            }
        delete resultlvl;
    }
    // end of gmconfig patch
    if (cmdStr == "add" || cmdStr == "login")
    {
        if (mgr->GetPlayerBot(guid))
        {
            PSendSysMessage("Bot already exists in world.");
            SetSentErrorMessage(true);
            return false;
        }
        CharacterDatabase.DirectPExecute("UPDATE characters SET online = 1 WHERE guid = '%u'", guid.GetCounter());
        mgr->AddPlayerBot(guid);
        PSendSysMessage("Bot added successfully.");
    }
    else if (cmdStr == "remove" || cmdStr == "logout")
    {
        if (!mgr->GetPlayerBot(guid))
        {
            PSendSysMessage("|cffff0000Bot can not be removed because bot does not exist in world.");
            SetSentErrorMessage(true);
            return false;
        }
        CharacterDatabase.DirectPExecute("UPDATE characters SET online = 0 WHERE guid = '%u'", guid.GetCounter());
        mgr->LogoutPlayerBot(guid);
        PSendSysMessage("Bot removed successfully.");
    }
    else if (cmdStr == "co" || cmdStr == "combatorder")
    {
        Unit *target = NULL;
        char *orderChar = strtok(NULL, " ");
        if (!orderChar)
        {
            PSendSysMessage("|cffff0000Syntax error:|cffffffff .bot co <botName> <order=reset|tank|assist|heal|protect> [targetPlayer]");
            SetSentErrorMessage(true);
            return false;
        }
        std::string orderStr = orderChar;
        if (orderStr == "protect" || orderStr == "assist")
        {
            char *targetChar = strtok(NULL, " ");
            ObjectGuid targetGUID = m_session->GetPlayer()->GetSelectionGuid();
            if (!targetChar && !targetGUID)
            {
                PSendSysMessage("|cffff0000Combat orders protect and assist expect a target either by selection or by giving target player in command string!");
                SetSentErrorMessage(true);
                return false;
            }
            if (targetChar)
            {
                std::string targetStr = targetChar;
                ObjectGuid targ_guid = sObjectMgr.GetPlayerGuidByName(targetStr.c_str());

                targetGUID.Set(targ_guid.GetRawValue());
            }
            target = ObjectAccessor::GetUnit(*m_session->GetPlayer(), targetGUID);
            if (!target)
            {
                PSendSysMessage("|cffff0000Invalid target for combat order protect or assist!");
                SetSentErrorMessage(true);
                return false;
            }
        }
        if (mgr->GetPlayerBot(guid) == NULL)
        {
            PSendSysMessage("|cffff0000Bot can not receive combat order because bot does not exist in world.");
            SetSentErrorMessage(true);
            return false;
        }
        mgr->GetPlayerBot(guid)->GetPlayerbotAI()->SetCombatOrderByStr(orderStr, target);
    }
    return true;
}

bool ChatHandler::HandleAutoBotCommand(char* args)
{
    if (!(m_session->GetSecurity() > SEC_PLAYER) && botConfig.GetBoolDefault("PlayerbotAI.DisableBots", false))
    {
        PSendSysMessage("|cffff0000Playerbot system is currently disabled!");
        SetSentErrorMessage(true);
        return false;
    }

    //// Well, for now anyway
    if (!m_session)
    {
        PSendSysMessage("|cffff0000You may only add bots from an active session");
        SetSentErrorMessage(true);
        return false;
    }

    if (!*args)
    {
        //PSendSysMessage("|cffff0000usage: add PLAYERNAME  or  remove PLAYERNAME");
        PSendSysMessage("|cffff0000Invalid arguments for .autobot command.");
        SetSentErrorMessage(true);
        return false;
    }

    std::string cmd = args;
    std::string subcommand = "";
    if (cmd.find(' '))
        subcommand = cmd.substr(cmd.find(' ', 1)+1);
    cmd = cmd.substr(0, cmd.find(' ', 1));

    // TODO: Will this really work with the regular PlayerbotMgr?
    // create the playerbot manager if it doesn't already exist
    PlayerbotMgr* mgr = m_session->GetPlayer()->GetPlayerbotMgr();
    if (!mgr)
    {
        mgr = new PlayerbotMgr(m_session->GetPlayer());
        m_session->GetPlayer()->SetPlayerbotMgr(mgr);
    }

    // TODO: Work out .autobot command structure and implement below (and above)

    if (0 == cmd.compare("add"))
    {
        // subcommand cannot be empty, must have a space 'purpose role', space must not be first or last character
        if (subcommand == "" || subcommand.find(' ', 1) > (subcommand.size()-1))
        {
            PSendSysMessage("|cffff0000.autobot add <worldpve|instance|worldpvp|battleground> <tank|dps|heal>");
            SetSentErrorMessage(true);
            return false;
        }

        std::string purpose = "";
        std::string role = "";
        purpose = subcommand.substr(0, subcommand.find(' ', 1));
        role = subcommand.substr(subcommand.find(' ', 1)+1);

        // further validate command input
        if ( (purpose != "worldpve" && purpose != "instance" && purpose != "worldpvp" && purpose != "battleground") ||
            (role != "tank" && role != "dps" && role != "heal") )
        {
            PSendSysMessage("|cffff0000.autobot add <worldpve | instance | worldpvp | battleground> <tank | dps | heal>");
            SetSentErrorMessage(true);
            return false;
        }

        // Create player here (not like you can add a player that doesn't exist)
        std::string name = "";

        // Select a random gender. GENDER_NONE is not an option for players.
        uint8 gender = ( (rand() % 2) == 1) ? GENDER_MALE : GENDER_FEMALE;

        std::vector<int> validChoices;

        if (role == "tank")
        {
            validChoices.push_back( CLASS_WARRIOR );
            validChoices.push_back( CLASS_PALADIN );
            validChoices.push_back( CLASS_DEATH_KNIGHT );
            validChoices.push_back( CLASS_DRUID );
        }
        else if (role == "dps")
        {
            validChoices.push_back( CLASS_WARRIOR );      // Because, clearly, everyone else is not in a war.
            validChoices.push_back( CLASS_PALADIN );      // Tankadin, Retadin, Holydin, all popular in WoW. Must be because they're everyone's Pal-adin.
            validChoices.push_back( CLASS_HUNTER );       // For those who love critters just a wee bit too much. Well, I hope it's only a wee bit.
            validChoices.push_back( CLASS_ROGUE );        // Because you want back stabber by your side. And not behind you. Not to be mistaken for make-up.
            validChoices.push_back( CLASS_PRIEST );       // Whether they're melting faces or healing them, your life is in their hands.
            validChoices.push_back( CLASS_DEATH_KNIGHT ); // Because DPS is so much more hardcore when your class has 'death' in it.
            validChoices.push_back( CLASS_SHAMAN );       // If you love having special poles sticking in the ground.
            validChoices.push_back( CLASS_MAGE );         // Because according to the dictionary only mages can use magic. ... Right?
            validChoices.push_back( CLASS_WARLOCK );      // For those who would love to see their enemy die after they've killed you.
            validChoices.push_back( CLASS_DRUID );        // Because no other class has a tank, melee dps, spell dps AND healer mode!
        }
        else if (role == "heal")
        {
            validChoices.push_back( CLASS_PALADIN );
            validChoices.push_back( CLASS_PRIEST );
            validChoices.push_back( CLASS_SHAMAN );
            validChoices.push_back( CLASS_DRUID );
        }
        // Select a random class.
        uint8 class_ = validChoices.at( (rand() % validChoices.size()) );

        validChoices.clear();

        // Get faction, may be important (e.g. for instancing you want same faction)
        uint32 raceMask = m_session->GetPlayer()->getRaceMask();
        bool bAnyFaction = false;

        if (RACEMASK_ALLIANCE & raceMask || (bAnyFaction && (RACEMASK_ALL_PLAYABLE & raceMask)))
        {
            switch (class_)
            {
            case CLASS_WARRIOR:
            case CLASS_PALADIN:
            case CLASS_ROGUE:
            case CLASS_PRIEST:
            case CLASS_DEATH_KNIGHT:
            case CLASS_MAGE:
            case CLASS_WARLOCK:
                validChoices.push_back( RACE_HUMAN ); // It's pronounced WHO-MAN, y'know?
                break;
            }

            switch (class_)
            {
            case CLASS_WARRIOR:
            case CLASS_PALADIN:
            case CLASS_HUNTER:
            case CLASS_ROGUE:
            case CLASS_PRIEST:
            case CLASS_DEATH_KNIGHT:
                validChoices.push_back( RACE_DWARF ); // Does the world really need more dorfs?
                break;
            }

            switch (class_)
            {
            case CLASS_WARRIOR:
            case CLASS_HUNTER:
            case CLASS_ROGUE:
            case CLASS_PRIEST:
            case CLASS_DEATH_KNIGHT:
            case CLASS_DRUID:
                validChoices.push_back( RACE_NIGHTELF ); // What happened to the day elves, I wonder?
                break;
            }

            switch (class_)
            {
            case CLASS_WARRIOR:
            case CLASS_ROGUE:
            case CLASS_DEATH_KNIGHT:
            case CLASS_MAGE:
            case CLASS_WARLOCK:
                validChoices.push_back( RACE_GNOME ); // Only gnomes could make engineering more error-prone than goblins.
                break;
            }

            switch (class_)
            {
            case CLASS_WARRIOR:
            case CLASS_PALADIN:
            case CLASS_HUNTER:
            case CLASS_PRIEST:
            case CLASS_DEATH_KNIGHT:
            case CLASS_SHAMAN:
            case CLASS_MAGE:
                validChoices.push_back( RACE_DRAENEI ); // Alien squidfaces in WoW. Who saw that one coming?
                break;
            }
        }

        if (RACEMASK_HORDE & raceMask || (bAnyFaction && (RACEMASK_ALL_PLAYABLE & raceMask)))
        {
            switch (class_)
            {
            case CLASS_WARRIOR:
            case CLASS_HUNTER:
            case CLASS_ROGUE:
            case CLASS_DEATH_KNIGHT:
            case CLASS_SHAMAN:
            case CLASS_WARLOCK:
                validChoices.push_back( RACE_ORC ); // Nothing against orcs, they're all just so very... green.
                break;
            }

            switch (class_)
            {
            case CLASS_WARRIOR:
            case CLASS_ROGUE:
            case CLASS_PRIEST:
            case CLASS_DEATH_KNIGHT:
            case CLASS_MAGE:
            case CLASS_WARLOCK:
                validChoices.push_back( RACE_UNDEAD ); // UN-dead. Doesn't that describe all of us? Hope so for your sake...
                break;
            }

            switch (class_)
            {
            case CLASS_WARRIOR:
            case CLASS_HUNTER:
            case CLASS_DEATH_KNIGHT:
            case CLASS_SHAMAN:
            case CLASS_DRUID:
                validChoices.push_back( RACE_TAUREN ); // Moo.
                break;
            }

            switch (class_)
            {
            case CLASS_WARRIOR:
            case CLASS_HUNTER:
            case CLASS_ROGUE:
            case CLASS_PRIEST:
            case CLASS_DEATH_KNIGHT:
            case CLASS_SHAMAN:
            case CLASS_MAGE:
                validChoices.push_back( RACE_TROLL ); // Trolls like war a lot for a race who likes to "tak'it easy, mon".
                break;
            }

            switch (class_)
            {
            case CLASS_PALADIN:
            case CLASS_HUNTER:
            case CLASS_ROGUE:
            case CLASS_PRIEST:
            case CLASS_DEATH_KNIGHT:
            case CLASS_MAGE:
            case CLASS_WARLOCK:
                validChoices.push_back( RACE_BLOODELF ); // Elves whose defining trait is that they have blood. Genius.
                break;
            }
        }

        // Select a random race.
        uint8 race_ = validChoices.at( (rand() % validChoices.size()) );

        validChoices.clear();

        /** This would've been an awesome bit of SQL writing to select a name. Unfortunately it took me 17.5 seconds to run.
        * The solution (far) below may be a bit less perfect (in reality, and quite a bit less perfect in theory) but at least it's fast.
        SELECT t1.`name`
        FROM playerbot_autobot_names AS t1, characters
        WHERE t1.`name` NOT IN
        (
                SELECT t1.name
                FROM playerbot_autobot_names as t1
                WHERE t1.in_use = 0 AND t1.name = characters.name
        )
        ORDER BY
        RAND() ASC
        LIMIT 1
        */

        // Possible variables to check for when selecting a name.
        //`name_id` mediumint(8) NOT NULL AUTO_INCREMENT UNIQUE
        //`name` varchar(13) NOT NULL UNIQUE
        //`gender` tinyint(3) unsigned NOT NULL
        //`race` smallint(5) unsigned NOT NULL
        //`class` smallint(5) unsigned NOT NULL
        //`purpose` int(11) unsigned NOT NULL
        //`priority` bit(1) NOT NULL
        //`in_use` bit(1) NOT NULL
        QueryResult *result = CharacterDatabase.Query("SELECT COUNT(*) FROM playerbot_autobot_names");
        if (!result)
        {
            DEBUG_LOG("[Playerbot] [HandleAutoBotCommand] [SQL] Failed to get a full count of names");
            return false;
        }
        Field *fields = result->Fetch();
        uint32 countAll = fields[0].GetUInt32();
        delete result;

        if (countAll == 0)
        {
            DEBUG_LOG("[Playerbot] [HandleAutoBotCommand] Full count of names == 0 - no names in DB?");
            return false;
        }

        // Distribute the randomness better
        // Needed because RAND_MAX can be only 32767 where names table can be 100k or even bigger
        // slightly 'benefits' low-ID names, but nothing worth using float's for or gimping randomness with randMultiplier-- at the end
        // why? 32767*3 = 131,068; 131068 % 130000 would mean the first 1068 names have double the chance of being selected.
        // Not perfect, a pity but certainly not worth fussing over. We have a need for speed...
        uint32 randMultiplier = 1;
        while ( (RAND_MAX * randMultiplier) < countAll)
            randMultiplier++;

        // TODO: Expand to use 'WHERE ... AND gender, race, class, purpose
        result = CharacterDatabase.Query("SELECT COUNT(*) FROM playerbot_autobot_names WHERE in_use = 0 AND priority = 1");
        if (!result) // couldn't get a count, character creation failed
        {
            DEBUG_LOG("[Playerbot] [HandleAutoBotCommand] [SQL] Failed to get a count of names (using priority 1)");
            return false;
        }

        fields = result->Fetch();
        uint32 count = fields[0].GetUInt32();
        delete result;

        if (count > 0)
        {
            // Select first name starting from a random number. NOTE: not totally random, but totally fast.
            // Could be there's 3 of 10 names free (ID 1, 2, 3) and you're looking for ID > 5
            // - so you may need to loop around (already established count > 0, so don't worry about that)
            // TODO: copy modified query's WHERE clause from above
            uint32 minId = (rand() * randMultiplier) % countAll;
            result = CharacterDatabase.PQuery("SELECT COUNT(*) FROM playerbot_autobot_names WHERE in_use = 0 AND priority = 1 AND name_id > %u", minId);
            if (!result) // couldn't get a count, character creation failed
            {
                DEBUG_LOG("[Playerbot] [HandleAutoBotCommand] [SQL] Failed to get a count of names (using priority 1) - minId");
                return false;
            }
            fields = result->Fetch();
            minId = (fields[0].GetUInt32() > 0) ? minId : 0;
            delete result; // AFTER you've read out fields[x]

            // TODO: copy modified query's WHERE clause from above
            result = CharacterDatabase.PQuery("SELECT name FROM playerbot_autobot_names WHERE in_use = 0 AND priority = 1 AND name_id > %u LIMIT 1", minId);
            if (!result) // couldn't get a name, character creation failed
            {
                DEBUG_LOG("[Playerbot] [HandleAutoBotCommand] [SQL] Failed to get a name (using priority 1)");
                return false;
            }

            fields = result->Fetch();
            name = fields[0].GetCppString();
            delete result;
        }
        else
        {
            // TODO: copy modified query's WHERE clause from above
            result = CharacterDatabase.Query("SELECT COUNT(*) FROM playerbot_autobot_names WHERE in_use = 0 AND priority = 0");
            if (!result)
            {
                DEBUG_LOG("[Playerbot] [HandleAutoBotCommand] [SQL] Failed to get a count of names (using priority 0)");
                return false;
            }

            fields = result->Fetch();
            count = fields[0].GetUInt32();
            delete result;

            if (count == 0)
            {
                // If you want to do a simple select with only 'WHERE in_use = 0' and no other where clauses, this would be it.
                // Not needed right now since all possibilities are accounted for (in_use = 0 and priority = 0|1)
                DEBUG_LOG("[Playerbot] [HandleAutoBotCommand] Failed to find a name. Possibly all names used up, else a bug.");
                return false;
            }

            uint32 minId = (rand() * randMultiplier) % countAll;
            result = CharacterDatabase.PQuery("SELECT COUNT(*) FROM playerbot_autobot_names WHERE in_use = 0 AND priority = 0 AND name_id > %u", minId);
            if (!result) // couldn't get a count, character creation failed
            {
                DEBUG_LOG("[Playerbot] [HandleAutoBotCommand] [SQL] Failed to get a count of names (using priority 0) - minId");
                return false;
            }
            fields = result->Fetch();
            minId = (fields[0].GetUInt32() > 0) ? minId : 0;
            delete result; // AFTER you've read out fields[x]

            // TODO: copy modified query's WHERE clause from above
            result = CharacterDatabase.PQuery("SELECT name FROM playerbot_autobot_names WHERE in_use = 0 AND priority = 0 AND name_id > %u LIMIT 1", minId);
            if (!result) // couldn't get a name, character creation failed
            {
                DEBUG_LOG("[Playerbot] [HandleAutoBotCommand] [SQL] Failed to get a name (using priority 0)");
                return false;
            }

            fields = result->Fetch();
            name = fields[0].GetCppString();
            delete result;
        }

        if (name == "") // We missed a failure somewhere?
        {
            DEBUG_LOG("[Playerbot] [HandleAutoBotCommand] Empty name where it shouldn't be.");
            return false;
        }

        // name is NOT one of the arguments, but let's make sure there's no mistakes in the names SQL table
        if (!normalizePlayerName(subcommand))
        {
            DEBUG_LOG("[Playerbot] [HandleAutoBotCommand] Failed to add an AutoBot, invalid name: %s", name.c_str());
            return false;
        }


        // This is the black box you now need to decipher. Good luck, me.
        //WorldPacket recv_data

        // extract other data required for player creating
        //uint8 skin, face, hairStyle, hairColor, facialHair, outfitId
        //recv_data >> skin >> face;
        //recv_data >> hairStyle >> hairColor >> facialHair >> outfitId;

        // Pretty sure this is a 'return' packet which we don't need (returning to server would be fairly pointless?)
        // On the other hand, it's been suggested packets may be the more elegant option. TBD.
        //WorldPacket data(SMSG_CHAR_CREATE, 1);                  // returned with diff.values in all cases

        //if(GetSecurity() == SEC_PLAYER)
        //{
            //if(uint32 mask = sWorld.getConfig(CONFIG_UINT32_CHARACTERS_CREATING_DISABLED))
            //{
                bool disabled = false;

                //Team team = Player::TeamForRace(race_);
                //switch(team)
                //{
                //    case ALLIANCE: disabled = mask & (1 << 0); break;
                //    case HORDE:    disabled = mask & (1 << 1); break;
                //}

                if(disabled)
                    return false; // (uint8)CHAR_CREATE_DISABLED;
            //}
        //}

        std::string DELME_class_ = "";
        switch (class_)
        {
        case CLASS_WARRIOR: DELME_class_ = "Warrior"; break;
        case CLASS_PALADIN: DELME_class_ = "Paladin"; break;
        case CLASS_HUNTER: DELME_class_ = "Hunter"; break;
        case CLASS_ROGUE: DELME_class_ = "Rogue"; break;
        case CLASS_PRIEST: DELME_class_ = "Priest"; break;
        case CLASS_DEATH_KNIGHT: DELME_class_ = "Death Knight"; break;
        case CLASS_SHAMAN: DELME_class_ = "Shaman"; break;
        case CLASS_MAGE: DELME_class_ = "Mage"; break;
        case CLASS_WARLOCK: DELME_class_ = "Warlock"; break;
        case CLASS_DRUID: DELME_class_ = "Druid"; break;
        default: DELME_class_ = "Broken";
        }
        std::string DELME_race_ = "";
        switch (race_)
        {
        case RACE_HUMAN: DELME_race_ = "RACE_HUMAN"; break;
        case RACE_DWARF: DELME_race_ = "RACE_DWARF"; break;
        case RACE_NIGHTELF: DELME_race_ = "RACE_NIGHTELF"; break;
        case RACE_GNOME: DELME_race_ = "RACE_GNOME"; break;
        case RACE_DRAENEI: DELME_race_ = "RACE_DRAENEI"; break;
        case RACE_ORC: DELME_race_ = "RACE_ORC"; break;
        case RACE_UNDEAD: DELME_race_ = "RACE_UNDEAD"; break;
        case RACE_TAUREN: DELME_race_ = "RACE_TAUREN"; break;
        case RACE_TROLL: DELME_race_ = "RACE_TROLL"; break;
        case RACE_BLOODELF: DELME_race_ = "RACE_BLOODELF"; break;
        default: DELME_race_ = "Broken";
        }
        PSendSysMessage("Artificial end of HandleAutoBotCommand: Name \"%s\"; Class: %s; Race: %s; Gender: %u", name.c_str(), DELME_class_.c_str(), DELME_race_.c_str(), gender);
        SetSentErrorMessage(true);

        DEBUG_LOG("Artificial end of HandleAutoBotCommand: Name \"%s\"; Class: %u; Race: %u; Gender: %u", name.c_str(), class_, race_, gender);
        return false;

        /*ChrClassesEntry const* classEntry = sChrClassesStore.LookupEntry(class_);
        ChrRacesEntry const* raceEntry = sChrRacesStore.LookupEntry(race_);

        if( !classEntry || !raceEntry )
        {
            data << (uint8)CHAR_CREATE_FAILED;
            SendPacket( &data );
            sLog.outError("Class: %u or Race %u not found in DBC (Wrong DBC files?) or Cheater?", class_, race_);
            return;
        }

        // prevent character creating Expansion race without Expansion account
        if (raceEntry->expansion > Expansion())
        {
            data << (uint8)CHAR_CREATE_EXPANSION;
            sLog.outError("Expansion %u account:[%d] tried to Create character with expansion %u race (%u)", Expansion(), GetAccountId(), raceEntry->expansion, race_);
            SendPacket( &data );
            return;
        }

        // prevent character creating Expansion class without Expansion account
        if (classEntry->expansion > Expansion())
        {
            data << (uint8)CHAR_CREATE_EXPANSION_CLASS;
            sLog.outError("Expansion %u account:[%d] tried to Create character with expansion %u class (%u)", Expansion(), GetAccountId(), classEntry->expansion, class_);
            SendPacket( &data );
            return;
        }

        // prevent character creating with invalid name
        if (!normalizePlayerName(name))
        {
            data << (uint8)CHAR_NAME_NO_NAME;
            SendPacket( &data );
            sLog.outError("Account:[%d] but tried to Create character with empty [name]", GetAccountId());
            return;
        }

        // check name limitations
        uint8 res = ObjectMgr::CheckPlayerName(name, true);
        if (res != CHAR_NAME_SUCCESS)
        {
            data << uint8(res);
            SendPacket( &data );
            return;
        }

        if (GetSecurity() == SEC_PLAYER && sObjectMgr.IsReservedName(name))
        {
            data << (uint8)CHAR_NAME_RESERVED;
            SendPacket( &data );
            return;
        }

        if (sObjectMgr.GetPlayerGuidByName(name))
        {
            data << (uint8)CHAR_CREATE_NAME_IN_USE;
            SendPacket( &data );
            return;
        }

        QueryResult *resultacct = LoginDatabase.PQuery("SELECT SUM(numchars) FROM realmcharacters WHERE acctid = '%u'", GetAccountId());
        if (resultacct)
        {
            Field *fields=resultacct->Fetch();
            uint32 acctcharcount = fields[0].GetUInt32();
            delete resultacct;

            if (acctcharcount >= sWorld.getConfig(CONFIG_UINT32_CHARACTERS_PER_ACCOUNT))
            {
                data << (uint8)CHAR_CREATE_ACCOUNT_LIMIT;
                SendPacket( &data );
                return;
            }
        }

        QueryResult *result = CharacterDatabase.PQuery("SELECT COUNT(guid) FROM characters WHERE account = '%u'", GetAccountId());
        uint8 charcount = 0;
        if ( result )
        {
            Field *fields = result->Fetch();
            charcount = fields[0].GetUInt8();
            delete result;

            if (charcount >= sWorld.getConfig(CONFIG_UINT32_CHARACTERS_PER_REALM))
            {
                data << (uint8)CHAR_CREATE_SERVER_LIMIT;
                SendPacket( &data );
                return;
            }
        }

        // speedup check for heroic class disabled case
        uint32 heroic_free_slots = sWorld.getConfig(CONFIG_UINT32_HEROIC_CHARACTERS_PER_REALM);
        if(heroic_free_slots == 0 && GetSecurity() == SEC_PLAYER && class_ == CLASS_DEATH_KNIGHT)
        {
            data << (uint8)CHAR_CREATE_UNIQUE_CLASS_LIMIT;
            SendPacket( &data );
            return;
        }

        // speedup check for heroic class disabled case
        uint32 req_level_for_heroic = sWorld.getConfig(CONFIG_UINT32_MIN_LEVEL_FOR_HEROIC_CHARACTER_CREATING);
        if(GetSecurity() == SEC_PLAYER && class_ == CLASS_DEATH_KNIGHT && req_level_for_heroic > sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL))
        {
            data << (uint8)CHAR_CREATE_LEVEL_REQUIREMENT;
            SendPacket( &data );
            return;
        }

        bool AllowTwoSideAccounts = sWorld.getConfig(CONFIG_BOOL_ALLOW_TWO_SIDE_ACCOUNTS) || GetSecurity() > SEC_PLAYER;
        CinematicsSkipMode skipCinematics = CinematicsSkipMode(sWorld.getConfig(CONFIG_UINT32_SKIP_CINEMATICS));

        bool have_same_race = false;

        // if 0 then allowed creating without any characters
        bool have_req_level_for_heroic = (req_level_for_heroic==0);

        if(!AllowTwoSideAccounts || skipCinematics == CINEMATICS_SKIP_SAME_RACE || class_ == CLASS_DEATH_KNIGHT)
        {
            QueryResult *result2 = CharacterDatabase.PQuery("SELECT level,race,class FROM characters WHERE account = '%u' %s",
                GetAccountId(), (skipCinematics == CINEMATICS_SKIP_SAME_RACE || class_ == CLASS_DEATH_KNIGHT) ? "" : "LIMIT 1");
            if(result2)
            {
                Team team_= Player::TeamForRace(race_);

                Field* field = result2->Fetch();
                uint8 acc_race  = field[1].GetUInt32();

                if(GetSecurity() == SEC_PLAYER && class_ == CLASS_DEATH_KNIGHT)
                {
                    uint8 acc_class = field[2].GetUInt32();
                    if(acc_class == CLASS_DEATH_KNIGHT)
                    {
                        if(heroic_free_slots > 0)
                            --heroic_free_slots;

                        if(heroic_free_slots == 0)
                        {
                            data << (uint8)CHAR_CREATE_UNIQUE_CLASS_LIMIT;
                            SendPacket( &data );
                            delete result2;
                            return;
                        }
                    }

                    if(!have_req_level_for_heroic)
                    {
                        uint32 acc_level = field[0].GetUInt32();
                        if(acc_level >= req_level_for_heroic)
                            have_req_level_for_heroic = true;
                    }
                }

                // need to check team only for first character
                // TODO: what to if account already has characters of both races?
                if (!AllowTwoSideAccounts)
                {
                    if (acc_race == 0 || Player::TeamForRace(acc_race) != team_)
                    {
                        data << (uint8)CHAR_CREATE_PVP_TEAMS_VIOLATION;
                        SendPacket( &data );
                        delete result2;
                        return;
                    }
                }

                // search same race for cinematic or same class if need
                // TODO: check if cinematic already shown? (already logged in?; cinematic field)
                while ((skipCinematics == CINEMATICS_SKIP_SAME_RACE && !have_same_race) || class_ == CLASS_DEATH_KNIGHT)
                {
                    if(!result2->NextRow())
                        break;

                    field = result2->Fetch();
                    acc_race = field[1].GetUInt32();

                    if(!have_same_race)
                        have_same_race = race_ == acc_race;

                    if(GetSecurity() == SEC_PLAYER && class_ == CLASS_DEATH_KNIGHT)
                    {
                        uint8 acc_class = field[2].GetUInt32();
                        if(acc_class == CLASS_DEATH_KNIGHT)
                        {
                            if(heroic_free_slots > 0)
                                --heroic_free_slots;

                            if(heroic_free_slots == 0)
                            {
                                data << (uint8)CHAR_CREATE_UNIQUE_CLASS_LIMIT;
                                SendPacket( &data );
                                delete result2;
                                return;
                            }
                        }

                        if(!have_req_level_for_heroic)
                        {
                            uint32 acc_level = field[0].GetUInt32();
                            if(acc_level >= req_level_for_heroic)
                                have_req_level_for_heroic = true;
                        }
                    }
                }
                delete result2;
            }
        }

        if(GetSecurity() == SEC_PLAYER && class_ == CLASS_DEATH_KNIGHT && !have_req_level_for_heroic)
        {
            data << (uint8)CHAR_CREATE_LEVEL_REQUIREMENT;
            SendPacket( &data );
            return;
        }

        Player *pNewChar = new Player(this);
        if (!pNewChar->Create(sObjectMgr.GeneratePlayerLowGuid(), name, race_, class_, gender, skin, face, hairStyle, hairColor, facialHair, outfitId))
        {
            // Player not create (race/class problem?)
            delete pNewChar;

            data << (uint8)CHAR_CREATE_ERROR;
            SendPacket( &data );

            return;
        }

        if ((have_same_race && skipCinematics == CINEMATICS_SKIP_SAME_RACE) || skipCinematics == CINEMATICS_SKIP_ALL)
            pNewChar->setCinematic(1);                          // not show intro

        pNewChar->SetAtLoginFlag(AT_LOGIN_FIRST);               // First login

        // Player created, save it now
        pNewChar->SaveToDB();
        charcount += 1;

        LoginDatabase.PExecute("DELETE FROM realmcharacters WHERE acctid= '%u' AND realmid = '%u'", GetAccountId(), realmID);
        LoginDatabase.PExecute("INSERT INTO realmcharacters (numchars, acctid, realmid) VALUES (%u, %u, %u)",  charcount, GetAccountId(), realmID);

        data << (uint8)CHAR_CREATE_SUCCESS;
        SendPacket( &data );

        std::string IP_str = GetRemoteAddress();
        BASIC_LOG("Account: %d (IP: %s) Create Character:[%s] (guid: %u)", GetAccountId(), IP_str.c_str(), name.c_str(), pNewChar->GetGUIDLow());
        sLog.outChar("Account: %d (IP: %s) Create Character:[%s] (guid: %u)", GetAccountId(), IP_str.c_str(), name.c_str(), pNewChar->GetGUIDLow());

        delete pNewChar;                                        // created only to call SaveToDB()

        return true;*/

        ObjectGuid guid = sObjectMgr.GetPlayerGuidByName(subcommand.c_str());
        if (guid == ObjectGuid() || (guid == m_session->GetPlayer()->GetObjectGuid()))
        {
            SendSysMessage(LANG_PLAYER_NOT_FOUND);
            SetSentErrorMessage(true);
            return false;
        }

        uint32 accountId = 1;

        /** This method will never work, account == 1 for autobots. Always.
        QueryResult *resultchar = CharacterDatabase.PQuery("SELECT COUNT(*) FROM characters WHERE online = '1' AND account = '%u'", m_session->GetAccountId());
        if (resultchar)
        {
            Field *fields = resultchar->Fetch();
            int acctcharcount = fields[0].GetUInt32();
            int maxnum = botConfig.GetIntDefault("PlayerbotAI.MaxNumBots", 9);
            if (!(m_session->GetSecurity() > SEC_PLAYER))
                if (acctcharcount > maxnum && (cmdStr == "add" || cmdStr == "login"))
                {
                    PSendSysMessage("|cffff0000You cannot summon anymore bots.(Current Max: |cffffffff%u)", maxnum);
                    SetSentErrorMessage(true);
                    delete resultchar;
                    return false;
                }
            delete resultchar;
        }
        */

        //if (mgr->GetPlayerBot(guid))
        //{
        //    PSendSysMessage("Bot already exists in world.");
        //    SetSentErrorMessage(true);
        //    return false;
        //}
        //CharacterDatabase.DirectPExecute("UPDATE characters SET online = 1 WHERE guid = '%u'", guid.GetCounter());
        //mgr->AddPlayerBot(guid);

        PSendSysMessage("Bot added successfully.");
        return true;
    }

    // TODO: fix this function. For now, don't open a gaping security hole
    PSendSysMessage("|cffff0000.autobot command not functional yet.");
    SetSentErrorMessage(true);
    return false;

    // remove. also likely.
    //else if (cmdStr == "remove" || cmdStr == "logout")
    //{
    //    if (!mgr->GetPlayerBot(guid))
    //    {
    //        PSendSysMessage("|cffff0000Bot can not be removed because bot does not exist in world.");
    //        SetSentErrorMessage(true);
    //        return false;
    //    }
    //    CharacterDatabase.DirectPExecute("UPDATE characters SET online = 0 WHERE guid = '%u'", guid.GetCounter());
    //    mgr->LogoutPlayerBot(guid);
    //    PSendSysMessage("Bot removed successfully.");
    //}

    // combatorder. tank, yes. Heal, yes.
    // Assist, yes (DPS) - WITHOUT TARGET. Automatically select tank, if multiple, tank with least assists.
    // Protect, no - AUTOMATICALLY protect healer. If multiple, protect healer with least protects.
    // Reset, hell no. You want a reset, you slave away working on your AltBot.
    //else if (cmdStr == "co" || cmdStr == "combatorder")
    //{
    //    Unit *target = NULL;
    //    char *orderChar = strtok(NULL, " ");
    //    if (!orderChar)
    //    {
    //        PSendSysMessage("|cffff0000Syntax error:|cffffffff .bot co <botName> <order=reset|tank|assist|heal|protect> [targetPlayer]");
    //        SetSentErrorMessage(true);
    //        return false;
    //    }
    //    std::string orderStr = orderChar;
    //    if (orderStr == "protect" || orderStr == "assist")
    //    {
    //        char *targetChar = strtok(NULL, " ");
    //        ObjectGuid targetGUID = m_session->GetPlayer()->GetSelectionGuid();
    //        if (!targetChar && !targetGUID)
    //        {
    //            PSendSysMessage("|cffff0000Combat orders protect and assist expect a target either by selection or by giving target player in command string!");
    //            SetSentErrorMessage(true);
    //            return false;
    //        }
    //        if (targetChar)
    //        {
    //            std::string targetStr = targetChar;
    //            ObjectGuid targ_guid = sObjectMgr.GetPlayerGuidByName(targetStr.c_str());

    //            targetGUID.Set(targ_guid.GetRawValue());
    //        }
    //        target = ObjectAccessor::GetUnit(*m_session->GetPlayer(), targetGUID);
    //        if (!target)
    //        {
    //            PSendSysMessage("|cffff0000Invalid target for combat order protect or assist!");
    //            SetSentErrorMessage(true);
    //            return false;
    //        }
    //    }
    //    if (mgr->GetPlayerBot(guid) == NULL)
    //    {
    //        PSendSysMessage("|cffff0000Bot can not receive combat order because bot does not exist in world.");
    //        SetSentErrorMessage(true);
    //        return false;
    //    }
    //    mgr->GetPlayerBot(guid)->GetPlayerbotAI()->SetCombatOrderByStr(orderStr, target);
    //}

    //return true;
    return false;
}
