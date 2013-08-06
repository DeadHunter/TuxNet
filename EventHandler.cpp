#include "EventHandler.h"
#include "asm_nop.h"
#pragma warning(disable: 4996)

EventHandler::EventHandler(RconConnection * rcon, boost::property_tree::ptree * pt)
	: rcon(rcon) ,needResponse(false), pt(pt)
{
	this->debug = (pt->get<string>("Service.Debug")=="true"?true:false);

	if(this->debug)
		out("EventHandler();", OUT_TYPE::DEBUG);

	this->InGameCommandsEnabled = (pt->get<string>("Service.InGameCommands")=="true"?true:false);
	this->InGameCommandPrefix = pt->get<string>("InGameCommands.Prefix");
	this->MySQLChatlogEnabled = (pt->get<string>("Service.MySQLChatlog")=="true"?true:false);
	this->StatisticsEnabled = (pt->get<string>("Service.Statistics")=="true"?true:false);

	this->CountdownRunning = false;
	this->needResponse = 0;

	this->thread_Countdown = nullptr;

	//automsg = new AutoMessage(this->rcon, pt->get<string>("AutoMessage.File"));
}
EventHandler::~EventHandler(void)
{
	if(this->debug)
		out("~EventHandler();", OUT_TYPE::DEBUG);
}


void EventHandler::AddEvent(TextRconPacket packet)
{
	if(this->debug)
		out("AddEvent();", OUT_TYPE::DEBUG);

	while(!this->queueMutex.try_lock())
		nop;

	if(packet.isValid() && packet.m_isResponse == false && packet.m_originatedOnServer == true)
	{

		EVENT _event;

		if(packet.m_data[0] == "player.onAuthenticated")
			_event.eventType = EVENT_TYPE::PLAYER_AUTH;

		else if(packet.m_data[0] == "player.onJoin")
			_event.eventType = EVENT_TYPE::PLAYER_JOIN;

		else if(packet.m_data[0] == "player.onLeave")
			_event.eventType = EVENT_TYPE::PLAYER_LEAVE;

		else if(packet.m_data[0] == "player.onSpawn")
			_event.eventType = EVENT_TYPE::PLAYER_SPAWN;

		else if(packet.m_data[0] == "player.onKill")
			_event.eventType = EVENT_TYPE::PLAYER_KILL;

		else if(packet.m_data[0] == "player.onChat")
			_event.eventType = EVENT_TYPE::PLAYER_CHAT;

		else if(packet.m_data[0] == "player.onSquadChange")
			_event.eventType = EVENT_TYPE::PLAYER_SQUAD_CHANGE;

		else if(packet.m_data[0] == "player.onTeamChange")
			_event.eventType = EVENT_TYPE::PLAYER_TEAM_CHANGE;

		else if(packet.m_data[0] == "punkBuster.onMessage")
			_event.eventType = EVENT_TYPE::PB_MESSAGE;

		else if(packet.m_data[0] == "server.onMaxPlayerCountChange")
			_event.eventType = EVENT_TYPE::SERVER_MAX_PLAYER_COUNT_CHANGE;

		else if(packet.m_data[0] == "server.onLevelLoaded")
			_event.eventType = EVENT_TYPE::SERVER_LEVEL_LOADED;

		else if(packet.m_data[0] == "server.onRoundOver")
			_event.eventType = EVENT_TYPE::SERVER_ROUND_OVER;

		else if(packet.m_data[0] == "server.onRoundOverPlayers")
			_event.eventType = EVENT_TYPE::SERVER_ROUND_OVER_PLAYER_STATS;

		else if(packet.m_data[0] == "server.onRoundOverTeamScores")
			_event.eventType = EVENT_TYPE::SERVER_ROUND_OVER_TEAM_SCORES;

		_event.packet = new TextRconPacket(packet);

		this->queue.push(_event);

	}
	this->queueMutex.unlock();
}
void EventHandler::DoWork(void)
{
	Enable();
	if(this->pt->get<string>("Service.ServerSize") == "true")
		ServerSizeChange();
	while(this->Running)
	{
		if(this->queue.size() > 0)
		{
			while(!this->queueMutex.try_lock())
				nop;

			EVENT _event = this->queue.front();
			this->queue.pop();

			this->queueMutex.unlock();

			if(this->debug)
				out(StringImplode(&_event.packet->m_data, " : "), OUT_TYPE::DEBUG);

			// Do some shit here with the packet troll-lol-lol
			if(_event.eventType == EVENT_TYPE::PLAYER_CHAT)
			{
				if(this->MySQLChatlogEnabled)
					MySQLChatlog(&_event.packet->m_data);

				if( this->InGameCommandsEnabled && _event.packet->m_data[1] != "Server" && StringStartsWith(_event.packet->m_data[2], this->InGameCommandPrefix) )
					InGameCommand(*_event.packet);
			}
			else if(_event.eventType == EVENT_TYPE::PLAYER_JOIN)
			{
				if(this->pt->get<string>("Service.ServerSize") == "true")
					ServerSizeChange();

				if(this->StatisticsEnabled)
					MySQLAddPlayer(_event.packet->m_data[1], _event.packet->m_data[2]);
			}
			else if(_event.eventType == EVENT_TYPE::PLAYER_LEAVE)
			{
				if(this->pt->get<string>("Service.ServerSize") == "true")
					ServerSizeChange();
			}
			else if(_event.eventType == EVENT_TYPE::PLAYER_KILL)
			{
				if(this->StatisticsEnabled)
					MySQLAddKillAndDeath(_event.packet->m_data[1], _event.packet->m_data[2], _event.packet->m_data[3], _event.packet->m_data[4]);
			}
			else if(_event.eventType == EVENT_TYPE::SERVER_ROUND_OVER)
			{
				if(this->pt->get<string>("Service.ServerSize") == "true")
					ServerSizeOnRoundOver();
			}
			else if(_event.eventType == EVENT_TYPE::SERVER_LEVEL_LOADED)
			{
				if(this->pt->get<string>("Service.ServerSize") == "true")
					ServerSizeOnLevelLoaded();
			}
			else if(_event.eventType == EVENT_TYPE::PLAYER_AUTH)
			{
				if(this->pt->get<string>("Service.ServerSize") == "true")
					ServerSizeOnLevelLoaded();
			}

		}
		if(this->thread_Countdown != nullptr && this->thread_Countdown->joinable() && !this->CountdownRunning)
		{
			this->thread_Countdown->join();
		}
		//this->automsg->DoWork();
		this_thread::sleep_for(std::chrono::milliseconds(100));
	}
}

void EventHandler::Enable()
{
	this->Running = true;
}
void EventHandler::Disable()
{
	this->Running = false;
}
bool EventHandler::canUseCommand(string sender, string right)
{
	if(this->debug)
		out("canUseCommands("+sender+", "+right+");", OUT_TYPE::DEBUG);

	try
	{
		sql::Driver				*driver;
		sql::Connection			*con;
		sql::PreparedStatement	*pstmt;
		sql::ResultSet			*result;
		string					constr = "tcp://"+pt->get<string>("MySQL.Host")+":"+pt->get<string>("MySQL.Port");

		driver = get_driver_instance();
		con = driver->connect(constr, pt->get<string>("MySQL.User"), pt->get<string>("MySQL.Password"));
		con->setSchema(pt->get<string>("MySQL.Database"));
		try
		{
			pstmt = con->prepareStatement("SELECT `right` FROM user_right INNER JOIN `user` ON (user_right.userId = `user`.id) WHERE nickname = ? AND `right` = ?");
			pstmt->setString(1, sender);
			pstmt->setString(2, right);
			result = pstmt->executeQuery();
			
			if(result->rowsCount() == 1)
				return true;

			delete result;
			delete pstmt;
		}
		catch(sql::SQLException &e)
		{
			out(e.what(), EventHandler::OUT_TYPE::ERR);
		}
		delete con;
	}
	catch(sql::SQLException &e)
	{
		out(e.what(), EventHandler::OUT_TYPE::ERR);
	}

	return false;

}
bool EventHandler::canUseCommands(string sender)
{
	if(this->debug)
		out("canUseCommands("+sender+");", OUT_TYPE::DEBUG);

	try
	{
		sql::Driver				*driver;
		sql::Connection			*con;
		sql::PreparedStatement	*pstmt;
		sql::ResultSet			*result;
		string					constr = "tcp://"+pt->get<string>("MySQL.Host")+":"+pt->get<string>("MySQL.Port");

		driver = get_driver_instance();
		con = driver->connect(constr, pt->get<string>("MySQL.User"), pt->get<string>("MySQL.Password"));
		con->setSchema(pt->get<string>("MySQL.Database"));
		try
		{
			pstmt = con->prepareStatement("SELECT nickname FROM `user` WHERE nickname = ?");
			pstmt->setString(1, sender);
			result = pstmt->executeQuery();
			
			if(result->rowsCount() == 1)
				return true;

			delete result;
			delete pstmt;
		}
		catch(sql::SQLException &e)
		{
			out(e.what(), EventHandler::OUT_TYPE::ERR);
		}
		delete con;
	}
	catch(sql::SQLException &e)
	{
		out(e.what(), EventHandler::OUT_TYPE::ERR);
	}

	return false;
}
bool EventHandler::canUseCommandWithRconMessage(string sender, string right)
{
	if(canUseCommand(sender, right))
		return true;
	else
		this->rcon->sendRequest(createWords("admin.say", "You can not use this command!", "player", sender.c_str()));
	return false;
}
int EventHandler::isAccountActive(string nickname)
{
	if(this->debug)
		out("isAccountActive("+nickname+");", OUT_TYPE::DEBUG);

	try
	{
		sql::Driver				*driver;
		sql::Connection			*con;
		sql::PreparedStatement	*pstmt;
		sql::ResultSet			*result;
		string					constr = "tcp://"+pt->get<string>("MySQL.Host")+":"+pt->get<string>("MySQL.Port");

		driver = get_driver_instance();
		con = driver->connect(constr, pt->get<string>("MySQL.User"), pt->get<string>("MySQL.Password"));
		con->setSchema(pt->get<string>("MySQL.Database"));
		try
		{
			pstmt = con->prepareStatement("SELECT disabled FROM `user` WHERE nickname = ?");
			pstmt->setString(1, nickname);
			result = pstmt->executeQuery();
			
			if(result->rowsCount() == 0)
				return -1;
			else
			{
				result->first();
				int disabled = result->getInt(1);

				if(disabled == 1)
					return 0;
				return 1;
			}

			delete result;
			delete pstmt;
		}
		catch(sql::SQLException &e)
		{
			out(e.what(), EventHandler::OUT_TYPE::ERR);
		}
		delete con;
	}
	catch(sql::SQLException &e)
	{
		out(e.what(), EventHandler::OUT_TYPE::ERR);
	}

	return 0;
}
string EventHandler::getAccountDisabledReason(string nickname)
{
	if(this->debug)
		out("getAccountDisabledReason("+nickname+");", OUT_TYPE::DEBUG);

	try
	{
		sql::Driver				*driver;
		sql::Connection			*con;
		sql::PreparedStatement	*pstmt;
		sql::ResultSet			*result;
		string					constr = "tcp://"+pt->get<string>("MySQL.Host")+":"+pt->get<string>("MySQL.Port");

		driver = get_driver_instance();
		con = driver->connect(constr, pt->get<string>("MySQL.User"), pt->get<string>("MySQL.Password"));
		con->setSchema(pt->get<string>("MySQL.Database"));
		try
		{
			pstmt = con->prepareStatement("SELECT reason FROM `user` WHERE nickname = ?");
			pstmt->setString(1, nickname);
			result = pstmt->executeQuery();
			
			if(result->rowsCount() == 0)
				return "";
			else
			{
				result->first();
				return result->getString(1);
			}

			delete result;
			delete pstmt;
		}
		catch(sql::SQLException &e)
		{
			out(e.what(), EventHandler::OUT_TYPE::ERR);
		}
		delete con;
	}
	catch(sql::SQLException &e)
	{
		out(e.what(), EventHandler::OUT_TYPE::ERR);
	}
}
vector<EventHandler::PLAYER> EventHandler::getPlayerlist(void)
{
	if(this->debug)
		out("getPlayerlist();", OUT_TYPE::DEBUG);

	vector<EventHandler::PLAYER> * plist = new vector<EventHandler::PLAYER>();

	do {

		this->needResponse = this->rcon->getSequence();
		this->rcon->sendRequest(createWords("admin.listPlayers", "all"));

		while(this->needResponse != 0)
			nop;

	} while(!this->response->isValid());

	for(unsigned int i = 11; i < this->response->m_data.size() - 7; i+=8)
		plist->push_back( EventHandler::PLAYER( this->response->m_data[i], this->response->m_data[i+1], atoi( this->response->m_data[i+2].c_str() ) ) );


	return *plist;
}
uint32_t EventHandler::getNumTeams()
{
	if(this->debug)
		out("getNumTeams();", OUT_TYPE::DEBUG);

	this->needResponse = this->rcon->getSequence();
	this->rcon->sendRequest(createWords("serverinfo"));

	while(this->needResponse != 0)
		nop;

	return atoi(response->m_data[8].c_str());
}

// Logging
string EventHandler::getCurrentDateTime()
{
	time_t now = time(0);
	std::tm * time = localtime(&now);
	char * tstr = new char[34];
	sprintf(tstr, "[%02d.%02d.%04d %02d:%02d:%02d] ", time->tm_mday, time->tm_mon + 1, 1900 + time->tm_year, time->tm_hour,
		time->tm_min, time->tm_sec);
	return string(tstr);
}
void EventHandler::out(string str, EventHandler::OUT_TYPE type)
{
	cout << getCurrentDateTime();

	switch(type)
	{
	case EventHandler::OUT_TYPE::DEBUG:
		cout << "[DEBUG] ";
		break;
	case EventHandler::OUT_TYPE::ERR:
		cout << "[ERROR] ";
		break;
	case EventHandler::OUT_TYPE::INFO:
		cout << "[INFO] ";
		break;
	case EventHandler::OUT_TYPE::WARNING:
		cout << "[WARNING] ";
		break;
	default:
		break;
	}

	cout << str << endl;
}
////

// Event Actions
void EventHandler::InGameCommand(TextRconPacket & packet)
{
	string sender = packet.m_data[1];
	string cmd = packet.m_data[2];
	int accountActive = isAccountActive(sender);

	if(this->debug)
		out("InGameCommand(); :: sender = "+sender, OUT_TYPE::DEBUG);
	std::cout << getCurrentDateTime() << sender << ": " << packet.m_data[2] << endl;
	
	if(accountActive == 1)
	{
		
		// restart
		if(cmd == this->InGameCommandPrefix + "restart" && canUseCommandWithRconMessage(sender, "command.restart"))
			InGameCommandRestart();
		// swapteams
		else if(cmd == this->InGameCommandPrefix + "swapteams" && canUseCommandWithRconMessage(sender, "command.swapteams"))
			InGameCommandSwapteams();
		// countdown
		else if(StringStartsWith(cmd, this->InGameCommandPrefix + "countdown") && canUseCommandWithRconMessage(sender, "command.countdown"))
			InGameCommandCountdown(packet);
		// kick
		else if(StringStartsWith(cmd, this->InGameCommandPrefix + "kick") && canUseCommandWithRconMessage(sender, "command.kick"))
			InGameCommandKick(packet);
		// move
		else if(StringStartsWith(cmd, this->InGameCommandPrefix + "move") && canUseCommandWithRconMessage(sender, "command.move"))
			InGameCommandMove(packet);
		// nuke
		else if(StringStartsWith(cmd, this->InGameCommandPrefix + "nuke") && canUseCommandWithRconMessage(sender, "command.nuke"))
			InGameCommandNuke(packet);
		// pban
		else if(StringStartsWith(cmd, this->InGameCommandPrefix + "pban") && canUseCommandWithRconMessage(sender, "command.pban"))
			InGameCommandPban(packet);
		// ban
		else if(StringStartsWith(cmd, this->InGameCommandPrefix + "ban") && canUseCommandWithRconMessage(sender, "command.ban"))
			InGameCommandBan(packet);
		// say
		else if(StringStartsWith(cmd, this->InGameCommandPrefix + "say") && canUseCommandWithRconMessage(sender, "command.say"))
			InGameCommandSay(packet);
		// slots
		else if(StringStartsWith(cmd, this->InGameCommandPrefix + "slots") && canUseCommandWithRconMessage(sender, "command.slots"))
			InGameCommandSlots(packet);
		// swap
		else if(StringStartsWith(cmd, this->InGameCommandPrefix + "swap") && canUseCommandWithRconMessage(sender, "command.swap"))
			InGameCommandSwap(packet);
		// win
		else if(StringStartsWith(cmd, this->InGameCommandPrefix + "win") && canUseCommandWithRconMessage(sender, "command.win"))
			InGameCommandWin(packet);
		// yell
		else if(StringStartsWith(cmd, this->InGameCommandPrefix + "yell") && canUseCommandWithRconMessage(sender, "command.yell"))
			InGameCommandYell(packet);
		// kill
		else if(StringStartsWith(cmd, this->InGameCommandPrefix + "kill") && canUseCommandWithRconMessage(sender, "command.kill"))
			InGameCommandKill(packet);

		// else if(cmd == this->InGameCommandPrefix + "" && canUseCommandWithRconMessage(sender, "command."))
		// else if(StringStartsWith(cmd, this->InGameCommandPrefix + "") && canUseCommandWithRconMessage(sender, "command."))
	}
	else if(accountActive == 0)
	{
		if(this->debug)
			out("Account ("+sender+") is disabled", OUT_TYPE::DEBUG);

		string reason = getAccountDisabledReason(sender);
		this->rcon->sendRequest(createWords("admin.say", string("Your Account is disabled. Reason: "+reason).c_str(), "player", sender.c_str())); 
	}

}
void EventHandler::MySQLChatlog(vector<string>* data)
{
	try
	{
		if(this->debug)
			out("MySQLChatlog(); :: INSERT INTO chatlog", OUT_TYPE::DEBUG);

		sql::Driver				*driver;
		sql::Connection			*con;
		sql::PreparedStatement	*pstmt;
		string					constr = "tcp://"+pt->get<string>("MySQL.Host")+":"+pt->get<string>("MySQL.Port");

		driver = get_driver_instance();
		con = driver->connect(constr, pt->get<string>("MySQL.User"), pt->get<string>("MySQL.Password"));
		con->setSchema(pt->get<string>("MySQL.Database"));
		try
		{
			pstmt = con->prepareStatement("INSERT INTO " + pt->get<string>("MySQL.TablePrefix") + "chatlog VALUES(NOW(), ?, ?, ?, ?, ?, ?)");
			pstmt->setString(1, data->at(1));
			pstmt->setString(2, data->at(2));
			pstmt->setString(3, data->at(3));

			if(data->size() > 4)
				pstmt->setString(4, data->at(4));
			else
				pstmt->setString(4, "");

			if(data->size() > 5)
				pstmt->setString(5, data->at(5));
			else
				pstmt->setString(5, "");

			if(StringStartsWith(data->at(2), this->InGameCommandPrefix))
				pstmt->setInt(6, 1);
			else
				pstmt->setInt(6, 0);

			pstmt->executeQuery();
			delete pstmt;
		}
		catch(sql::SQLException &e)
		{
			out(e.what(), EventHandler::OUT_TYPE::ERR);
		}
		delete con;
	}
	catch(sql::SQLException &e)
	{
		out(e.what(), EventHandler::OUT_TYPE::ERR);
	}
}
void EventHandler::MySQLAddPlayer(string soldiername, string eaid)
{
	try
	{
		if(this->debug)
			out("MySQLAddPlayer(); :: INSERT INTO player, INSERT INTO player_alias, UPDATE player", OUT_TYPE::DEBUG);

		sql::Driver				*driver;
		sql::Connection			*con;
		sql::PreparedStatement	*pstmt;
		string					constr = "tcp://"+pt->get<string>("MySQL.Host")+":"+pt->get<string>("MySQL.Port");

		driver = get_driver_instance();
		con = driver->connect(constr, pt->get<string>("MySQL.User"), pt->get<string>("MySQL.Password"));
		con->setSchema(pt->get<string>("MySQL.Database"));

		try
		{
			pstmt = con->prepareStatement("INSERT IGNORE INTO player (eaid, soldiername, kills, deaths, headshots, suicides) VALUES(?, ?, 0, 0, 0, 0)");
			pstmt->setString(1, eaid);
			pstmt->setString(2, soldiername);
			pstmt->executeQuery();
			delete pstmt;
		}
		catch(sql::SQLException &e)
		{
			out(e.what(), EventHandler::OUT_TYPE::ERR);
		}

		try
		{
			pstmt = con->prepareStatement("INSERT IGNORE INTO player_alias (eaid, alias) VALUES(?, ?)");
			pstmt->setString(1, eaid);
			pstmt->setString(2, soldiername);
			pstmt->executeQuery();
			delete pstmt;
		}
		catch(sql::SQLException &e)
		{
			out(e.what(), EventHandler::OUT_TYPE::ERR);
		}

		try
		{
			pstmt = con->prepareStatement("UPDATE player SET soldiername = ? WHERE eaid = ?");
			pstmt->setString(1, soldiername);
			pstmt->setString(2, eaid);
			pstmt->executeQuery();
			delete pstmt;
		}
		catch(sql::SQLException &e)
		{
			out(e.what(), EventHandler::OUT_TYPE::ERR);
		}

		delete con;
	}
	catch(sql::SQLException &e)
	{
		out(e.what(), EventHandler::OUT_TYPE::ERR);
	}
}
void EventHandler::MySQLAddKillAndDeath(string killer, string killed, string weapon, string headshot)
{
	try
	{
		if(this->debug)
			out("MySQLAddKillAndDeath(); :: INSERT INTO player, INSERT INTO player_alias, UPDATE player", OUT_TYPE::DEBUG);

		sql::Driver				*driver;
		sql::Connection			*con;
		sql::PreparedStatement	*pstmt;
		sql::ResultSet			*result;
		string					constr = "tcp://"+pt->get<string>("MySQL.Host")+":"+pt->get<string>("MySQL.Port");

		driver = get_driver_instance();
		con = driver->connect(constr, pt->get<string>("MySQL.User"), pt->get<string>("MySQL.Password"));
		con->setSchema(pt->get<string>("MySQL.Database"));

		// Prüfen, ob Killer und Killed in DB
		if(killer.length() > 0)
		{
			try
			{
				pstmt = con->prepareStatement("SELECT COUNT(*) FROM player WHERE soldiername = ?");
				pstmt->setString(1, killer);
				result = pstmt->executeQuery();
				delete pstmt;

				if(result->rowsCount() == 0)
				{
					if(this->debug)
						out("MySQLAddKillAndDeath(); :: Player (Killer) not exists. Fetching data", OUT_TYPE::DEBUG);

					vector<EventHandler::PLAYER> playerlist = getPlayerlist();
					vector<EventHandler::PLAYER>::iterator it = std::find_if(playerlist.begin(), playerlist.end(), [killer](EventHandler::PLAYER player) {
						if(boost::to_lower_copy(player.nickname).find(killer) != string::npos)
							return true;			
						return false;
					});

					if(it != playerlist.end())
					{
						MySQLAddPlayer(it->nickname, it->eaid);
					}
				}
			}
			catch(sql::SQLException &e)
			{
				out(e.what(), EventHandler::OUT_TYPE::ERR);
			}
		}
		if(killed.length() > 0)
		{
			try
			{
				pstmt = con->prepareStatement("SELECT COUNT(*) FROM player WHERE soldiername = ?");
				pstmt->setString(1, killed);
				result = pstmt->executeQuery();
				delete pstmt;

				if(result->rowsCount() == 0)
				{
					if(this->debug)
						out("MySQLAddKillAndDeath(); :: Player (Killed) not exists. Fetching data", OUT_TYPE::DEBUG);

					this->needResponse = this->rcon->getSequence();
					this->rcon->sendRequest(createWords("admin.listPlayers", "all"));

					while(this->needResponse != 0)
						nop;

					vector<EventHandler::PLAYER> playerlist = getPlayerlist();
					vector<EventHandler::PLAYER>::iterator it = std::find_if(playerlist.begin(), playerlist.end(), [killed](EventHandler::PLAYER player) {
						if(boost::to_lower_copy(player.nickname).find(killed) != string::npos)
							return true;			
						return false;
					});

					if(it != playerlist.end())
					{
						MySQLAddPlayer(it->nickname, it->eaid);
					}
				}
			}
			catch(sql::SQLException &e)
			{
				out(e.what(), EventHandler::OUT_TYPE::ERR);
			}
		}

		try
		{
			pstmt = con->prepareStatement("INSERT INTO killLog (eaidKiller, eaidVictim, weapon, isHeadshot, isSuicide, isAdminkill, `timestamp`) VALUES ((SELECT eaid FROM player WHERE soldiername = ?), (SELECT eaid FROM player WHERE soldiername = ?), ?, ?, ?, ?, NOW())");
			pstmt->setString(1, killer);
			pstmt->setString(2, killed);
			pstmt->setString(3, weapon);
			pstmt->setInt(4, (headshot=="true"?1:0));
			pstmt->setInt(5, (killer==killed?1:0));
			pstmt->setInt(6, (killer.length()==0?1:0));
			pstmt->executeQuery();
			delete pstmt;
		}
		catch(sql::SQLException &e)
		{
			out(e.what(), EventHandler::OUT_TYPE::ERR);
		}

		if(killer != killed)
		{
			try
			{
				pstmt = con->prepareStatement("UPDATE player SET kills = kills + 1 WHERE soldiername = ?");
				pstmt->setString(1, killer);
				pstmt->executeQuery();
				delete pstmt;
			}
			catch(sql::SQLException &e)
			{
				out(e.what(), EventHandler::OUT_TYPE::ERR);
			}
			try
			{
				if(string("Melee,Weapons/Knife/Knife,Knife_RazorBlade").find(weapon) != string::npos)
				{
					pstmt = con->prepareStatement("INSERT INTO knifeLog (eaidKiller, eaidVictim, `timestamp`) VALUES((SELECT eaid FROM player WHERE soldiername = ?), (SELECT eaid FROM player WHERE soldiername = ?), NOW())");
					pstmt->setString(1, killer);
					pstmt->setString(2, killed);
					pstmt->executeQuery();
					delete pstmt;
				}
			}
			catch(sql::SQLException &e)
			{
				out(e.what(), EventHandler::OUT_TYPE::ERR);
			}

			try
			{
				if(headshot == "true")
				{
					pstmt = con->prepareStatement("UPDATE player SET headshots = headshots + 1 WHERE soldiername = ?");
					pstmt->setString(1, killer);
					pstmt->executeQuery();
					delete pstmt;
				}
			}
			catch(sql::SQLException &e)
			{
				out(e.what(), EventHandler::OUT_TYPE::ERR);
			}
		}

		try
		{
			if(killer.length() > 0)
			{
				pstmt = con->prepareStatement("UPDATE player SET deaths = deaths + 1 WHERE soldiername = ?");
				pstmt->setString(1, killed);
				pstmt->executeQuery();
				delete pstmt;
			}
		}
		catch(sql::SQLException &e)
		{
			out(e.what(), EventHandler::OUT_TYPE::ERR);
		}

		try
		{
			if(killer == killed)
			{
				pstmt = con->prepareStatement("UPDATE player SET suicides = suicides + 1 WHERE soldiername = ?");
				pstmt->setString(1, killed);
				pstmt->executeQuery();
				delete pstmt;
			}
		}
		catch(sql::SQLException &e)
		{
			out(e.what(), EventHandler::OUT_TYPE::ERR);
		}

		delete con;
	}
	catch(sql::SQLException &e)
	{
		out(e.what(), EventHandler::OUT_TYPE::ERR);
	}
}
////

// String Tools
inline void EventHandler::StringExplode(string str, string separator, vector<string>* results)
{
	if(this->debug)
		out("StringExplode();", OUT_TYPE::DEBUG);

	int found;
	found = str.find_first_of(separator);
	while(found != string::npos){
		if(found > 0){
			results->push_back(str.substr(0,found));
		}
		str = str.substr(found+1);
		found = str.find_first_of(separator);
	}
	if(str.length() > 0){
		results->push_back(str);
	}
}
inline string EventHandler::StringImplode(vector<string>* input, string separator)
{
	string result = "";

	for(string data : *input)
		result += data + separator;

	result.erase(result.length() - separator.length(), result.length());

	return result;
}
inline string EventHandler::StringImplode(vector<string>::iterator from, vector<string>::iterator to, string separator)
{
	string result = "";

	for(vector<string>::iterator it = from; it != to; it++)
		result += *it + separator;

	result.erase(result.length() - separator.length(), result.length());

	return result;
}
bool EventHandler::StringStartsWith(string str, string startStr)
{
	if(this->debug)
		out("StringStartsWith();", OUT_TYPE::DEBUG);

	size_t pos;
	if((pos = str.find(startStr)) == 0)
		return true;
	return false;
}
string EventHandler::convertInt(int number)
{
	stringstream ss;//create a stringstream
	ss << number;//add number to the stream
	return ss.str();//return a string with the contents of the stream
}
////

// ServerSize
void EventHandler::ServerSizeOnRoundOver()
{
	this->rcon->sendRequest(createWords("vars.maxPlayers", this->pt->get<string>("ServerSize.SlotsMaxSize").c_str()));
}
void EventHandler::ServerSizeOnLevelLoaded()
{
	ServerSizeChange();
}
void EventHandler::ServerSizeChange()
{
	int SlotsStart = this->pt->get<int>("ServerSize.SlotsStart");
	int SlotsMax = this->pt->get<int>("ServerSize.SlotsMaxSize");
	int SlotsIncrease = this->pt->get<int>("ServerSize.SlotsIncrease");
	int TicketsStart = this->pt->get<int>("ServerSize.TicketsStart");
	int TicketsIncrease = this->pt->get<int>("ServerSize.TicketsIncrease");

	do
	{
		this->needResponse = this->rcon->getSequence();
		this->rcon->sendRequest(createWords("serverinfo"));

		while(this->needResponse != 0)
			nop;
	} while(!this->response->isValid());

	int playersOnline = atoi(this->response->m_data[2].c_str());
	
	int multi;
	for(multi = 0; (SlotsStart + multi * SlotsIncrease) - playersOnline < 1 && SlotsStart + multi * SlotsIncrease <= SlotsMax - SlotsIncrease; multi++) {}
		
	int tickets = TicketsStart + multi * TicketsIncrease;
	int slots = SlotsStart + multi * SlotsIncrease;

	if(this->debug)
		out("ServerSizeChange(); Players-Online: " + convertInt(playersOnline) + " | Slots: " + convertInt(slots) + " | Tickets: " + convertInt(tickets), OUT_TYPE::DEBUG);

	this->rcon->sendRequest(createWords("vars.maxPlayers", convertInt(slots).c_str()));
	this->rcon->sendRequest(createWords("vars.gameModeCounter", convertInt(tickets).c_str()));
}
////

// InGame Commands
void EventHandler::InGameCommandBan(TextRconPacket & packet)
{
	vector<string> args;
	StringExplode(packet.m_data[2], " ", &args);

	string nickname;
	string reason = "No reason";
	
	if(args.size() > 1)
	{
		nickname = args[1];
		if(args.size() > 2)
			reason = StringImplode(args.begin() + 2, args.end());

		vector<EventHandler::PLAYER> playerlist = getPlayerlist();
		vector<EventHandler::PLAYER>::iterator it = std::find_if(playerlist.begin(), playerlist.end(), [nickname](EventHandler::PLAYER player) {
			if(boost::to_lower_copy(player.nickname).find( boost::to_lower_copy(nickname) ) != string::npos)
				return true;			
			return false;
		});

		if(it != playerlist.end())
		{
			// Spieler gefunden
			this->rcon->sendRequest( createWords("banList.add", "guid", it->eaid.c_str(), "rounds", "1", reason.c_str()) );

			string out = "Banned player " + it->nickname;
			out += " (1 Round). Reason: ";
			out += reason;
			this->rcon->sendRequest(createWords("admin.say", out.c_str(), "all"));
		}
		else
		{
			// Spieler nicht gefunden
			this->rcon->sendRequest(createWords("admin.say", "Could not find player", "player", packet.m_data[1].c_str()));
		}
	}
}
void EventHandler::InGameCommandPban(TextRconPacket & packet)
{
	vector<string> args;
	StringExplode(packet.m_data[2], " ", &args);

	string nickname;
	string reason = "No reason";
	
	if(args.size() > 1)
	{
		nickname = args[1];
		if(args.size() > 2)
			reason = StringImplode(args.begin() + 2, args.end());

		vector<EventHandler::PLAYER> playerlist = getPlayerlist();
		vector<EventHandler::PLAYER>::iterator it = std::find_if(playerlist.begin(), playerlist.end(), [nickname](EventHandler::PLAYER player) {
			if(boost::to_lower_copy(player.nickname).find( boost::to_lower_copy(nickname) ) != string::npos)
				return true;			
			return false;
		});

		if(it != playerlist.end())
		{
			// Spieler gefunden
			this->rcon->sendRequest( createWords("banList.add", "guid", it->eaid.c_str(), "perm", reason.c_str()) );

			string out = "Banned player " + it->nickname;
			out += " (permanent). Reason: ";
			out += reason;
			this->rcon->sendRequest(createWords("admin.say", out.c_str(), "all"));
		}
		else
		{
			// Spieler nicht gefunden
			this->rcon->sendRequest(createWords("admin.say", "Could not find player", "player", packet.m_data[1].c_str()));
		}
	}
}
void EventHandler::InGameCommandCountdown(TextRconPacket & packet)
{
	vector<string> args;
	StringExplode(packet.m_data[2], " ", &args);

	string sender = packet.m_data[1];

	if(args.size() == 1)
	{
		// Vordefinierter Countdown
		if(!this->CountdownRunning) {
			delete this->thread_Countdown;
			this->CountdownRunning = true;

			this->thread_Countdown = new std::thread([&]() {

				std::this_thread::sleep_for(std::chrono::seconds(1));
				this->rcon->sendRequest(createWords("admin.say", "-- 04 --", "all"));
				std::this_thread::sleep_for(std::chrono::seconds(1));
				this->rcon->sendRequest(createWords("admin.say", "-- 03 --", "all"));
				std::this_thread::sleep_for(std::chrono::seconds(1));
				this->rcon->sendRequest(createWords("admin.say", "-- 02 --", "all"));
				std::this_thread::sleep_for(std::chrono::seconds(1));
				this->rcon->sendRequest(createWords("admin.say", "-- 01 --", "all"));
				std::this_thread::sleep_for(std::chrono::seconds(1));
				this->rcon->sendRequest(createWords("admin.say", "-- GO --", "all"));
				this->CountdownRunning = false;

			});
		} else {
			this->rcon->sendRequest(createWords("admin.say", "Es laueft bereits ein Countdown", "player", sender.c_str()));
		}
	}
	else if(args.size() == 2)
	{
		// Benutzerdefinierter Countdown
		int seconds = atoi(args[1].c_str());
		if(seconds >= 5 && seconds <= 30)
		{
			if(!this->CountdownRunning) {
				delete this->thread_Countdown;
				this->CountdownRunning = true;
				std::this_thread::sleep_for(std::chrono::seconds(1));

				this->thread_Countdown = new std::thread([&]() {
					for(int sec = seconds ; sec > 0; sec--)
					{				
						char out[9];
						sprintf(out, "-- %02d --", sec);
						this->rcon->sendRequest(createWords("admin.say", out, "all"));
						std::this_thread::sleep_for(std::chrono::seconds(1));
					}

					this->rcon->sendRequest(createWords("admin.say", "-- GO --", "all"));
					this->CountdownRunning = false;
				});
			} else {
				this->rcon->sendRequest(createWords("admin.say", "Es laueft bereits ein Countdown", "player", sender.c_str()));
			}
		}
		else
		{
			string out = this->InGameCommandPrefix;
			out += "countdown [sec: 5-30]";
			this->rcon->sendRequest(createWords("admin.say", out.c_str(), "player", sender.c_str()));
			out = this->InGameCommandPrefix;
			out += "countdown (Startet einen 4 Sek. Countdown)";
			this->rcon->sendRequest(createWords("admin.say", out.c_str(), "player", sender.c_str()));
		}
	}
}
void EventHandler::InGameCommandKick(TextRconPacket & packet)
{
	vector<string> args;
	StringExplode(packet.m_data[2], " ", &args);

	string nickname;
	string reason = "No reason";
	
	if(args.size() > 1)
	{
		nickname = args[1];
		if(args.size() > 2)
			reason = StringImplode(args.begin() + 2, args.end());

		vector<EventHandler::PLAYER> playerlist = getPlayerlist();
		vector<EventHandler::PLAYER>::iterator it = std::find_if(playerlist.begin(), playerlist.end(), [nickname](EventHandler::PLAYER player) {
			if(boost::to_lower_copy(player.nickname).find( boost::to_lower_copy(nickname) ) != string::npos)
				return true;			
			return false;
		});

		if(it != playerlist.end())
		{
			// Spieler gefunden
			this->rcon->sendRequest( createWords("admin.kickPlayer", it->nickname.c_str(), reason.c_str()) );

			string out = "Kicked player " + it->nickname;
			out += ". Reason: ";
			out += reason;
			this->rcon->sendRequest(createWords("admin.say", out.c_str(), "all"));
		}
		else
		{
			// Spieler nicht gefunden
			this->rcon->sendRequest(createWords("admin.say", "Could not find player", "player", packet.m_data[1].c_str()));
		}
	}
}
void EventHandler::InGameCommandKill(TextRconPacket & packet)
{
	vector<string> args;
	StringExplode(packet.m_data[2], " ", &args);

	string nickname;
	string reason = "No reason";
	
	if(args.size() > 1)
	{
		nickname = args[1];
		if(args.size() > 2)
			reason = StringImplode(args.begin() + 2, args.end());

		vector<EventHandler::PLAYER> playerlist = getPlayerlist();
		vector<EventHandler::PLAYER>::iterator it = std::find_if(playerlist.begin(), playerlist.end(), [nickname](EventHandler::PLAYER player) {
			if(boost::to_lower_copy(player.nickname).find( boost::to_lower_copy(nickname) ) != string::npos)
				return true;			
			return false;
		});

		if(it != playerlist.end())
		{
			// Spieler gefunden
			this->rcon->sendRequest( createWords("admin.killPlayer", it->nickname.c_str()) );

			string out = it->nickname + " killed by admin. Reason: " + reason;
			this->rcon->sendRequest(createWords("admin.say", out.c_str(), "all"));
		}
		else
		{
			// Spieler nicht gefunden
			this->rcon->sendRequest(createWords("admin.say", "Could not find player", "player", packet.m_data[1].c_str()));
		}
	}
}
void EventHandler::InGameCommandMove(TextRconPacket & packet)
{
	vector<string> args;
	StringExplode(packet.m_data[2], " ", &args);
	string sender = packet.m_data[1];

	string nickname;
	int team = 0, squad = 0;
	uint32_t numTeams = getNumTeams();

	if(args.size() > 2)
	{
		nickname = args[1];
	
		if( boost::to_lower_copy(args[2]) == "us" )
			team = 1;
		else if( boost::to_lower_copy(args[2]) == "ru" )
			team = 2;
		else
			sscanf(args[2].c_str(), "%d", &team);
		
		if(args.size() == 4)
		{
			sscanf(args[3].c_str(), "%d", &squad);
		}

		vector<EventHandler::PLAYER> playerlist = getPlayerlist();
		vector<EventHandler::PLAYER>::iterator it = std::find_if(playerlist.begin(), playerlist.end(), [nickname](EventHandler::PLAYER player) {
			if(boost::to_lower_copy(player.nickname).find( boost::to_lower_copy(nickname) ) != string::npos)
				return true;			
			return false;
		});

		if(it != playerlist.end())
		{
			// Spieler gefunden
			if(team > 0 && team <= numTeams)
			{
				if(squad >= 0 && squad <= 32)
				{
					this->rcon->sendRequest( createWords("admin.movePlayer", it->nickname.c_str(), convertInt(team).c_str(), convertInt(squad).c_str(), "true") );

					string out = "Moved player " + it->nickname;
					out += " to team ";
					out += convertInt(team);
					out += ", squad ";
					out += convertInt(squad);

					this->rcon->sendRequest(createWords("admin.say", out.c_str(), "player", sender.c_str()));
					this->rcon->sendRequest(createWords("admin.say", "Moved by admin", "player", it->nickname.c_str()));
				}
				else
				{
					this->rcon->sendRequest(createWords("admin.say", "Invalid squad", "player", packet.m_data[1].c_str()));
				}
			}
			else
			{
				this->rcon->sendRequest(createWords("admin.say", "Invalid team", "player", packet.m_data[1].c_str()));
			}
		}
		else
		{
			// Spieler nicht gefunden
			this->rcon->sendRequest(createWords("admin.say", "Could not find player", "player", packet.m_data[1].c_str()));
		}
	}
}
void EventHandler::InGameCommandNuke(TextRconPacket & packet)
{
	vector<string> args;
	StringExplode(packet.m_data[2], " ", &args);

	string nickname;
	int team = -1;

	vector<EventHandler::PLAYER> plist = getPlayerlist();

	if(args.size() == 1)
	{
		for(EventHandler::PLAYER player : plist)
			this->rcon->sendRequest(createWords("admin.killPlayer", player.nickname.c_str()));

		ostringstream out;
		out << "Nuked all players" << team;

		this->rcon->sendRequest(createWords("admin.say", out.str().c_str(), "all"));
	}
	else if(args.size() == 2)
	{
		if( boost::to_lower_copy(args[1]) == "us" )
			team = 1;
		else if( boost::to_lower_copy(args[1]) == "ru" )
			team = 2;
		else
			sscanf(args[1].c_str(), "%d", &team);

		if(team > 0 && team <= getNumTeams())
		{
			for(EventHandler::PLAYER player : plist)
					if(player.teamId == team)
						this->rcon->sendRequest(createWords("admin.killPlayer", player.nickname.c_str()));

			ostringstream out;
			out << "Nuked team #" << team;

			this->rcon->sendRequest(createWords("admin.say", out.str().c_str(), "all"));
		}
		else
		{
			this->rcon->sendRequest(createWords("admin.say", "Invalid team", "player", packet.m_data[1].c_str()));
		}
	}
}
void EventHandler::InGameCommandRestart()
{
	this->rcon->sendRequest(createWords("mapList.restartRound"));
}
void EventHandler::InGameCommandSay(TextRconPacket & packet)
{
	vector<string> args;
	StringExplode(packet.m_data[2], " ", &args);

	if(args.size() > 1)
	{
		this->rcon->sendRequest(createWords("admin.say", StringImplode(args.begin()+1, args.end()).c_str(), "all"));
	}
}
void EventHandler::InGameCommandYell(TextRconPacket & packet)
{
	vector<string> args;
	StringExplode(packet.m_data[2], " ", &args);

	if(args.size() > 1)
	{
		this->rcon->sendRequest(createWords("admin.yell", StringImplode(args.begin()+1, args.end()).c_str(), "10", "all"));
	}
}
void EventHandler::InGameCommandSlots(TextRconPacket & packet)
{
	vector<string> args;
	StringExplode(packet.m_data[2], " ", &args);

	if(args.size() == 2)
	{
		int slots = 0;
		if(sscanf(args[1].c_str(), "%d", &slots))
		{
			this->rcon->sendRequest(createWords("vars.maxPlayers", convertInt(slots).c_str()));
		}
	}
}
void EventHandler::InGameCommandSwap(TextRconPacket & packet)
{
	vector<string> args;
	StringExplode(packet.m_data[2], " ", &args);

	if(args.size() == 2)
	{
		string nickname = args[1];

		vector<EventHandler::PLAYER> playerlist = getPlayerlist();
		vector<EventHandler::PLAYER>::iterator it = std::find_if(playerlist.begin(), playerlist.end(), [nickname](EventHandler::PLAYER player) {
			if(boost::to_lower_copy(player.nickname).find( boost::to_lower_copy(nickname) ) != string::npos)
				return true;			
			return false;
		});

		if(it != playerlist.end())
		{
			uint32_t numTeams = getNumTeams();
			if(it->teamId == numTeams)
				it->teamId = 1;
			else
				it->teamId++;

			stringstream teamId;
			string _teamId;
			teamId << it->teamId;
			teamId >> _teamId;
			this->rcon->sendRequest( createWords("admin.movePlayer", it->nickname.c_str(), _teamId.c_str(), "0", "true") );

			string out = "Swapped player " + it->nickname;
			this->rcon->sendRequest(createWords("admin.say", out.c_str(), "player", packet.m_data[1].c_str()));
		}
		else
			this->rcon->sendRequest(createWords("admin.say", "player not found", "player", packet.m_data[1].c_str()));
	}
}
void EventHandler::InGameCommandSwapteams()
{
	vector<EventHandler::PLAYER> plist = getPlayerlist();
	uint32_t numTeams = getNumTeams();

	for(EventHandler::PLAYER player : plist)
	{
		if(player.teamId == numTeams)
			player.teamId = 1;
		else
			player.teamId++;

		stringstream teamId;
		string _teamId;
		teamId << player.teamId;
		teamId >> _teamId;
		this->rcon->sendRequest( createWords("admin.movePlayer", player.nickname.c_str(), _teamId.c_str(), "0", "true") );
	}

	this->rcon->sendRequest( createWords("admin.say", "Swapped all players", "all") );
}
void EventHandler::InGameCommandWin(TextRconPacket & packet)
{
	vector<string> args;
	StringExplode(packet.m_data[2], " ", &args);

	if(args.size() == 2)
	{
		uint32_t winner;

		if(boost::to_lower_copy(args[1]) == "us")
			winner = 1;
		else if(boost::to_lower_copy(args[1]) == "ru")
			winner = 2;
		else
			winner = atoi(args[1].c_str());		

		if(winner > 0 && winner <= getNumTeams())
			this->rcon->sendRequest( createWords("mapList.endRound", convertInt(winner).c_str()) );
		else
			this->rcon->sendRequest(createWords("admin.say", "Invalid team", "player", packet.m_data[1].c_str()));
	}
}
////