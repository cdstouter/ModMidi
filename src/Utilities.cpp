/* 
 * File:   Utilities.cpp
 * Author: caleb
 * 
 * Created on December 11, 2017, 10:26 AM
 */

#include <string>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>
#include <jansson.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <chrono>

#include "Utilities.h"

void findAndReplaceAll(std::string &data, std::string toSearch, std::string replaceStr) {
    size_t pos = data.find(toSearch);
    while(pos != std::string::npos) {
        data.replace(pos, toSearch.size(), replaceStr);
        pos = data.find(toSearch, pos + toSearch.size());
    }
}

bool sendMessage(int socket, std::mutex *mutex, std::string command, std::string data, std::string &response) {
    std::lock_guard<std::mutex> guard(*mutex);
    auto start = std::chrono::steady_clock::now();
    std::string message = command;
    if (data.length() > 0) message += " " + data;
    message += "\n";
    if (send(socket, message.c_str(), message.length(), 0) < 0) {
        std::cout << "sendMessage: send failed" << std::endl;
        return false;
    }
    
    struct pollfd fd;
    int ret;
    fd.fd = socket;
    fd.events = POLLIN;

    std::string return_data = "";
    char server_reply[4096];
    size_t pos;
    size_t bytes;
    while ((ret = poll(&fd, 1, 10000)) > 0) {
        bytes = recv(socket, server_reply, sizeof(server_reply), 0);
        if (bytes < 0) {
            std::cout << "sendMessage: error while receiving from server" << std::endl;
            return false;
        }
        std::string chunk(server_reply, bytes);
        pos = chunk.find('\n');
        if (pos == std::string::npos) {
            return_data += chunk;
        } else {
            return_data += chunk.substr(0, pos);
            break;
        }
    }
    if (ret <= 0) {
        std::cout << "sendMessage: error while receiving data from server" << std::endl;
        return false;
    }
    
    // since we terminate with a newline, unescape any escaped newlines
    findAndReplaceAll(return_data, "\\n", "\n");
    
    response = return_data;
    auto end = std::chrono::steady_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "sendMessage: " << command << " took " << diff.count() << " msec" << std::endl;
    return true;
}

bool getPedalboardList(int socket, std::mutex *socket_mutex, std::vector<ModPedalboard> &pedalboardList, std::mutex *mutex) {
    std::string response;
    bool status;
    
    // get the current bank
    status = sendMessage(socket, socket_mutex, "get_bank", "", response);
    if (!status) {
        std::cout << "getPedalboardList error" << std::endl;
        if (mutex) std::lock_guard<std::mutex> guard(*mutex);
        pedalboardList.clear();
        return false;
    }
    json_error_t err;
    json_t *root = json_loads(response.c_str(), JSON_DECODE_ANY, &err);
    if (!root) {
        std::cout << "getPedalboardList: unable to parse JSON" << std::endl;
        if (mutex) std::lock_guard<std::mutex> guard(*mutex);
        pedalboardList.clear();
        return false;
    }
    if (!json_is_object(root)) {
        std::cout << "getPedalboardList: JSON root is not object" << std::endl;
        if (mutex) std::lock_guard<std::mutex> guard(*mutex);
        pedalboardList.clear();
        json_decref(root);
        return false;
    }
    json_t *okay = json_object_get(root, "okay");
    if (!okay || !json_is_boolean(okay) || !json_boolean_value(okay)) {
        std::cout << "getPedalboardList: not okay" << std::endl;
        if (mutex) std::lock_guard<std::mutex> guard(*mutex);
        pedalboardList.clear();
        json_decref(root);
        return false;
    }
    json_t *bank = json_object_get(root, "bank");
    if (!bank || !json_is_object(bank)) {
        std::cout << "getPedalboardList: bank not found" << std::endl;
        if (mutex) std::lock_guard<std::mutex> guard(*mutex);
        pedalboardList.clear();
        json_decref(root);
        return false;
    }
    json_t *pedalboards = json_object_get(bank, "pedalboards");
    if (!pedalboards) {
        std::cout << "getPedalboardList: no pedalboards array" << std::endl;
        if (mutex) std::lock_guard<std::mutex> guard(*mutex);
        pedalboardList.clear();
        json_decref(root);
        return false;
    }
    if (!json_is_array(pedalboards)) {
        std::cout << "getPedalboardList: pedalboards is not an array" << std::endl;
        if (mutex) std::lock_guard<std::mutex> guard(*mutex);
        pedalboardList.clear();
        json_decref(root);
        return false;
    }

    std::vector<ModPedalboard> tempPedalboardList;
    for (size_t i=0; i<json_array_size(pedalboards); i++) {
        json_t *data = json_array_get(pedalboards, i);
        if (!json_is_object(data)) {
            std::cout << "getPedalboardList: pedalboard is not an object" << std::endl;
            if (mutex) std::lock_guard<std::mutex> guard(*mutex);
            pedalboardList.clear();
            json_decref(root);
            return false;
        }
        json_t *title, *bundle;
        title = json_object_get(data, "title");
        bundle = json_object_get(data, "bundle");
        if (!title || !bundle) {
            std::cout << "getPedalboardList: could not find title & bundle in pedalboard" << std::endl;
            if (mutex) std::lock_guard<std::mutex> guard(*mutex);
            pedalboardList.clear();
            json_decref(root);
            return false;
        }
        if (!json_is_string(title) || !json_is_string(bundle)) {
            std::cout << "getPedalboardList: title & bundle aren't strings" << std::endl;
            if (mutex) std::lock_guard<std::mutex> guard(*mutex);
            pedalboardList.clear();
            json_decref(root);
            return false;
        }
        ModPedalboard mb;
        mb.title = json_string_value(title);
        mb.bundle = json_string_value(bundle);
        tempPedalboardList.push_back(mb);
    }
    if (mutex) std::lock_guard<std::mutex> guard(*mutex);
    pedalboardList = tempPedalboardList;
    json_decref(root);
    return true;
}

bool getPresetList(int socket, std::mutex *socket_mutex, std::vector<std::string> &presetList, std::mutex *mutex) {
    std::string response;
    bool status;
    
    // get the pedalboard preset list
    status = sendMessage(socket, socket_mutex, "get_presets", "", response);
    if (!status) {
        std::cout << "getPresetList error" << std::endl;
        if (mutex) std::lock_guard<std::mutex> guard(*mutex);
        presetList.clear();
        return false;
    }
    json_error_t err;
    json_t *root = json_loads(response.c_str(), JSON_DECODE_ANY, &err);
    if (!root) {
        std::cout << "getPresetList: unable to parse JSON" << std::endl;
        if (mutex) std::lock_guard<std::mutex> guard(*mutex);
        presetList.clear();
        return false;
    }
    if (!json_is_object(root)) {
        std::cout << "getPresetList: JSON root is not object" << std::endl;
        if (mutex) std::lock_guard<std::mutex> guard(*mutex);
        presetList.clear();
        json_decref(root);
        return false;
    }
    json_t *okay = json_object_get(root, "okay");
    if (!okay || !json_is_boolean(okay) || !json_boolean_value(okay)) {
        std::cout << "getPresetList: not okay" << std::endl;
        if (mutex) std::lock_guard<std::mutex> guard(*mutex);
        presetList.clear();
        json_decref(root);
        return false;
    }
    json_t *presets = json_object_get(root, "presets");
    if (!presets || !json_is_object(presets)) {
        std::cout << "getPresetList: presets not found" << std::endl;
        if (mutex) std::lock_guard<std::mutex> guard(*mutex);
        presetList.clear();
        json_decref(root);
        return false;
    }
    
    // look for the pedalboard presets one by one
    if (mutex) std::lock_guard<std::mutex> guard(*mutex);
    presetList.clear();
    for (int i=0; i<999; i++) {
        json_t *preset = json_object_get(presets, std::to_string(i).c_str());
        if (!preset) break;
        if (!json_is_string(preset)) break;
        presetList.push_back(json_string_value(preset));
    }
    json_decref(root);
    return true;
}

bool getCurrentPedalboardAndPreset(int socket, std::mutex *socket_mutex, std::vector<ModPedalboard> pedalboardList, int &currentPedalboard, int &currentPreset, unsigned int &pedalboardOffset, std::mutex *mutex) {
    std::string response;
    bool status;
    
    // get the pedalboard preset list
    status = sendMessage(socket, socket_mutex, "get_pedalboard", "", response);
    if (!status) {
        std::cout << "getCurrentPedalboardAndPreset error" << std::endl;
        if (mutex) std::lock_guard<std::mutex> guard(*mutex);
        currentPedalboard = currentPreset = -1;
        return false;
    }
    json_error_t err;
    json_t *root = json_loads(response.c_str(), JSON_DECODE_ANY, &err);
    if (!root) {
        std::cout << "getCurrentPedalboardAndPreset: unable to parse JSON" << std::endl;
        if (mutex) std::lock_guard<std::mutex> guard(*mutex);
        currentPedalboard = currentPreset = -1;
        return false;
    }
    if (!json_is_object(root)) {
        std::cout << "getCurrentPedalboardAndPreset: root is not an object" << std::endl;
        if (mutex) std::lock_guard<std::mutex> guard(*mutex);
        currentPedalboard = currentPreset = -1;
        json_decref(root);
        return false;
    }
    json_t *okay = json_object_get(root, "okay");
    if (!okay || !json_is_boolean(okay) || !json_boolean_value(okay)) {
        std::cout << "getCurrentPedalboardAndPreset: not okay" << std::endl;
        if (mutex) std::lock_guard<std::mutex> guard(*mutex);
        currentPedalboard = currentPreset = -1;
        json_decref(root);
        return false;
    }
    json_t *pedalboard = json_object_get(root, "pedalboard");
    if (!pedalboard || !json_is_object(pedalboard)) {
        std::cout << "getCurrentPedalboardAndPreset: pedalboard not found" << std::endl;
        if (mutex) std::lock_guard<std::mutex> guard(*mutex);
        currentPedalboard = currentPreset = -1;
        json_decref(root);
        return false;
    }
    json_t *json_path = json_object_get(pedalboard, "path");
    json_t *json_preset = json_object_get(pedalboard, "preset");
    if (!json_path || !json_preset || !json_is_string(json_path) || !json_is_integer(json_preset)) {
        std::cout << "getCurrentPedalboardAndPreset: unable to find the correct data" << std::endl;
        if (mutex) std::lock_guard<std::mutex> guard(*mutex);
        currentPedalboard = currentPreset = -1;
        json_decref(root);
        return false;
    }
    if (mutex) std::lock_guard<std::mutex> guard(*mutex);
    std::string path = json_string_value(json_path);
    currentPedalboard = -1;
    for (size_t i=0; i<pedalboardList.size(); i++) {
        if (pedalboardList.at(i).bundle == path) {
            currentPedalboard = i;
            break;
        }
    }
    currentPreset = json_integer_value(json_preset);
    // make sure the pedalboard offset is within range
    if (pedalboardOffset >= pedalboardList.size()) {
        if (currentPedalboard < 0) {
            pedalboardOffset = 0;
        } else {
            pedalboardOffset = (currentPedalboard / 5) * 5;
        }
    }
    json_decref(root);
    return true;
}

bool getCurrentBPM(int socket, std::mutex *socket_mutex, double &currentBPM, std::mutex *mutex) {
    std::string response;
    bool status;
    
    // get the pedalboard preset list
    status = sendMessage(socket, socket_mutex, "get_bpm", "", response);
    if (!status) {
        std::cout << "getCurrentBPM error" << std::endl;
        return false;
    }
    json_error_t err;
    json_t *root = json_loads(response.c_str(), JSON_DECODE_ANY | JSON_DECODE_INT_AS_REAL, &err);
    if (!root) {
        std::cout << "getCurrentBPM: unable to parse JSON" << std::endl;
        return false;
    }
    if (!json_is_object(root)) {
        std::cout << "getCurrentBPM: root is not an object" << std::endl;
        json_decref(root);
        return false;
    }
    json_t *okay = json_object_get(root, "okay");
    if (!okay || !json_is_boolean(okay) || !json_boolean_value(okay)) {
        std::cout << "getCurrentBPM: not okay" << std::endl;
        json_decref(root);
        return false;
    }
    json_t *bpm = json_object_get(root, "bpm");
    if (!bpm || !json_is_number(bpm)) {
        std::cout << "getCurrentBPM: bpm does not exist or is not a number" << std::endl;
        json_decref(root);
        return false;
    }
    if (mutex) std::lock_guard<std::mutex> guard(*mutex);
    currentBPM = json_number_value(bpm);
    json_decref(root);
    return true;
}

bool setBPM(int socket, std::mutex *socket_mutex, double bpm) {
    std::string response;
    bool status;
    
    // get the pedalboard preset list
    status = sendMessage(socket, socket_mutex, "set_bpm", "{\"bpm\": " + std::to_string(bpm) + "}", response);
    if (!status) {
        std::cout << "setBPM error" << std::endl;
        return false;
    }
    json_error_t err;
    json_t *root = json_loads(response.c_str(), JSON_DECODE_ANY, &err);
    if (!root) {
        std::cout << "setBPM: unable to parse JSON" << std::endl;
        return false;
    }
    if (!json_is_object(root)) {
        std::cout << "setBPM: root is not an object" << std::endl;
        json_decref(root);
        return false;
    }
    json_t *okay = json_object_get(root, "okay");
    if (!okay || !json_is_boolean(okay) || !json_boolean_value(okay)) {
        std::cout << "setBPM: not okay" << std::endl;
        json_decref(root);
        return false;
    }
    json_decref(root);
    return true;
}

bool loadPreset(int socket, std::mutex *socket_mutex, int preset) {
    std::string response;
    bool status;
    
    // get the pedalboard preset list
    status = sendMessage(socket, socket_mutex, "load_preset", "{\"id\": " + std::to_string(preset) + "}", response);
    if (!status) {
        std::cout << "loadPreset error" << std::endl;
        return false;
    }
    json_error_t err;
    json_t *root = json_loads(response.c_str(), JSON_DECODE_ANY, &err);
    if (!root) {
        std::cout << "loadPreset: unable to parse JSON" << std::endl;
        return false;
    }
    if (!json_is_object(root)) {
        std::cout << "loadPreset: root is not an object" << std::endl;
        json_decref(root);
        return false;
    }
    json_t *okay = json_object_get(root, "okay");
    if (!okay || !json_is_boolean(okay) || !json_boolean_value(okay)) {
        std::cout << "loadPreset: not okay" << std::endl;
        json_decref(root);
        return false;
    }
    json_decref(root);
    return true;
}

bool loadPedalboard(int socket, std::mutex *socket_mutex, int pedalboard) {
    std::string response;
    bool status;
    
    // get the pedalboard preset list
    status = sendMessage(socket, socket_mutex, "load_pedalboard", "{\"id\": " + std::to_string(pedalboard) + "}", response);
    if (!status) {
        std::cout << "loadPedalboard error" << std::endl;
        return false;
    }
    json_error_t err;
    json_t *root = json_loads(response.c_str(), JSON_DECODE_ANY, &err);
    if (!root) {
        std::cout << "loadPedalboard: unable to parse JSON" << std::endl;
        return false;
    }
    if (!json_is_object(root)) {
        std::cout << "loadPedalboard: root is not an object" << std::endl;
        json_decref(root);
        return false;
    }
    json_t *okay = json_object_get(root, "okay");
    if (!okay || !json_is_boolean(okay) || !json_boolean_value(okay)) {
        std::cout << "loadPedalboard: not okay" << std::endl;
        json_decref(root);
        return false;
    }
    json_decref(root);
    return true;
}