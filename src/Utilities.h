/* 
 * File:   Utilities.h
 * Author: caleb
 *
 * Created on December 11, 2017, 10:26 AM
 */

#ifndef UTILITIES_H
#define UTILITIES_H

#include "http.h"

#include <string>
#include <mutex>

class ModPedalboard {
public:
    std::string title;
    std::string bundle;
};

std::string urlEncode(const std::string &value);

bool httpGet(std::string url, std::string *response, std::string *contentType);

bool httpPost(std::string url, void const *data, size_t data_size, std::string *response, std::string *contentType);

bool modReset(std::string hostname);

bool getPedalboardList(std::string hostname, std::vector<ModPedalboard> &pedalboardList, std::mutex *mutex);

bool getPresetList(std::string hostname, std::vector<std::string> &presetList, std::mutex *mutex);

bool getCurrentPedalboardAndPreset(std::string hostname, std::vector<ModPedalboard> pedalboardList, int &currentPedalboard, int &currentPreset, std::mutex *mutex);

bool getCurrentBPM(std::string hostname, double &currentBPM, std::mutex *mutex);

bool setBPM(std::string hostname, double bpm);

bool loadPreset(std::string hostname, int preset);

bool loadPedalboard(std::string hostname, int pedalboard);

#endif /* UTILITIES_H */

