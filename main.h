#ifndef MAIN_H

#define MAIN_H

#pragma warning(disable: 4996)

#include <iostream>
#include <string>
#include <iomanip>
#include <thread>
#include <chrono>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>

#include "RconPacket.h"
#include "RconConnection.h"
#include "EventHandler.h"
#include "asm_nop.h"

using namespace std;

#define REVISION "48"

#endif