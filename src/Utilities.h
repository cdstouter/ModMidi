/* 
 * File:   Utilities.h
 * Author: caleb
 *
 * Created on December 11, 2017, 10:26 AM
 */

#ifndef UTILITIES_H
#define UTILITIES_H

#include <string>
#include <mutex>

class ModPedalboard {
public:
    std::string title;
    std::string bundle;
};

bool sendMessage(int socket, std::mutex *mutex, std::string command, std::string data, std::string &response);

bool getPedalboardList(int socket, std::mutex *socket_mutex, std::vector<ModPedalboard> &pedalboardList, std::mutex *mutex);

bool getPresetList(int socket, std::mutex *socket_mutex, std::vector<std::string> &presetList, std::mutex *mutex);

bool getCurrentPedalboardAndPreset(int socket, std::mutex *socket_mutex, std::vector<ModPedalboard> pedalboardList, int &currentPedalboard, int &currentPreset, unsigned int &pedalboardOffset, std::mutex *mutex);

bool getCurrentBPM(int socket, std::mutex *socket_mutex, double &currentBPM, std::mutex *mutex);

bool setBPM(int socket, std::mutex *socket_mutex, double bpm);

bool loadPreset(int socket, std::mutex *socket_mutex, int preset);

bool loadPedalboard(int socket, std::mutex *socket_mutex, int pedalboard);

#endif /* UTILITIES_H */

