/* 
 * File:   Utilities.cpp
 * Author: caleb
 * 
 * Created on December 11, 2017, 10:26 AM
 */

#define HTTP_IMPLEMENTATION

#include <string>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>
#include <jansson.h>

#include "Utilities.h"

std::string urlEncode(const std::string &value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (auto &c : value) {
        // Keep alphanumeric and other accepted characters intact
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
            continue;
        }
        // Any other characters are percent-encoded
        escaped << std::uppercase;
        escaped << '%' << std::setw(2) << int((unsigned char) c);
        escaped << std::nouppercase;
    }

    return escaped.str();
}

bool httpPost(std::string url, void const *data, size_t data_size, std::string *response, std::string *contentType) {
    *contentType = "";
    
    http_t* request = http_post(url.c_str(), data, data_size, NULL);
    if( !request ) {
        *response = "error creating HTTP request";
        return false;
    }

    http_status_t status = HTTP_STATUS_PENDING;
    int prev_size = -1;
    while(status == HTTP_STATUS_PENDING) {
        status = http_process(request);
        if(prev_size != (int)request->response_size) {
            prev_size = (int)request->response_size;
        }
    }

    if(status == HTTP_STATUS_FAILED) {
        *response = std::to_string(request->status_code) + ": " + request->reason_phrase;
        http_release(request);
        return false;
    }
    
    *contentType = request->content_type;
    *response = (char const *)request->response_data;

    http_release(request);
    return true;
}

bool httpGet(std::string url, std::string *response, std::string *contentType) {
    *contentType = "";
    
    http_t* request = http_get(url.c_str(), NULL);
    if( !request ) {
        *response = "error creating HTTP request";
        return false;
    }

    http_status_t status = HTTP_STATUS_PENDING;
    int prev_size = -1;
    while(status == HTTP_STATUS_PENDING) {
        status = http_process(request);
        if(prev_size != (int)request->response_size) {
            prev_size = (int)request->response_size;
        }
    }

    if(status == HTTP_STATUS_FAILED) {
        *response = std::to_string(request->status_code) + ": " + request->reason_phrase;
        http_release(request);
        return false;
    }
    
    *contentType = request->content_type;
    *response = (char const *)request->response_data;

    http_release(request);
    return true;
}

bool modReset(std::string hostname) {
    std::string response, contentType;
    std::string url;
    bool status;
    
    // get the current bank
    url = "http://" + hostname + "/reset";
    status = httpGet(url, &response, &contentType);
    if (!status) {
        std::cout << "modReset HTTP error: " << response << std::endl;
        return false;
    }
    json_error_t err;
    json_t *root = json_loads(response.c_str(), JSON_DECODE_ANY | JSON_DECODE_INT_AS_REAL, &err);
    if (!root) {
        std::cout << "modReset: unable to parse JSON" << std::endl;
        return false;
    }
    if (!json_is_boolean(root)) {
        std::cout << "modReset: root is not a boolean" << std::endl;
        json_decref(root);
        return false;
    }
    bool val = json_boolean_value(root);
    json_decref(root);
    return val;
}

bool getPedalboardList(std::string hostname, std::vector<ModPedalboard> &pedalboardList, std::mutex *mutex) {
    std::string response, contentType;
    std::string url;
    bool status;
    
    // get the current bank
    url = "http://" + hostname + "/banks/current";
    status = httpGet(url, &response, &contentType);
    if (!status) {
        std::cout << "getPedalboardList HTTP error: " << response << std::endl;
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
    if (json_is_null(root)) {
        // current bank is empty
        if (mutex) std::lock_guard<std::mutex> guard(*mutex);
        pedalboardList.clear();
        json_decref(root);
        return true;
    } else if (!json_is_object(root)) {
        std::cout << "getPedalboardList: JSON root is not object or null" << std::endl;
        if (mutex) std::lock_guard<std::mutex> guard(*mutex);
        pedalboardList.clear();
        json_decref(root);
        return false;
    }
    json_t *pedalboards = json_object_get(root, "pedalboards");
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

bool getPresetList(std::string hostname, std::vector<std::string> &presetList, std::mutex *mutex) {
    std::string response, contentType;
    std::string url;
    bool status;
    
    // get the pedalboard preset list
    url = "http://" + hostname + "/pedalpreset/list";
    status = httpGet(url, &response, &contentType);
    if (!status) {
        std::cout << "getPresetList HTTP error: " << response << std::endl;
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
        std::cout << "getPresetList: JSON root is not object or null" << std::endl;
        if (mutex) std::lock_guard<std::mutex> guard(*mutex);
        presetList.clear();
        json_decref(root);
        return false;
    }
    
    // look for the pedalboard presets one by one
    if (mutex) std::lock_guard<std::mutex> guard(*mutex);
    presetList.clear();
    for (int i=0; i<999; i++) {
        json_t *preset = json_object_get(root, std::to_string(i).c_str());
        if (!preset) break;
        if (!json_is_string(preset)) break;
        presetList.push_back(json_string_value(preset));
    }
    json_decref(root);
    return true;
}

bool getCurrentPedalboardAndPreset(std::string hostname, std::vector<ModPedalboard> pedalboardList, int &currentPedalboard, int &currentPreset, unsigned int &pedalboardOffset, std::mutex *mutex) {
    std::string response, contentType;
    std::string url;
    bool status;
    
    // get the pedalboard preset list
    url = "http://" + hostname + "/pedalboard/current";
    status = httpGet(url, &response, &contentType);
    if (!status) {
        std::cout << "getCurrentPedalboardAndPreset HTTP error: " << response << std::endl;
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
    json_t *json_path = json_object_get(root, "path");
    json_t *json_preset = json_object_get(root, "preset");
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

bool getCurrentBPM(std::string hostname, double &currentBPM, std::mutex *mutex) {
    std::string response, contentType;
    std::string url;
    bool status;
    
    // get the pedalboard preset list
    url = "http://" + hostname + "/tempo/get";
    status = httpGet(url, &response, &contentType);
    if (!status) {
        std::cout << "getCurrentBPM HTTP error: " << response << std::endl;
        return false;
    }
    json_error_t err;
    json_t *root = json_loads(response.c_str(), JSON_DECODE_ANY | JSON_DECODE_INT_AS_REAL, &err);
    if (!root) {
        std::cout << "getCurrentBPM: unable to parse JSON" << std::endl;
        return false;
    }
    if (!json_is_number(root)) {
        std::cout << "getCurrentBPM: root is not a number" << std::endl;
        json_decref(root);
        return false;
    }
    if (mutex) std::lock_guard<std::mutex> guard(*mutex);
    currentBPM = json_number_value(root);
    json_decref(root);
    return true;
}

bool setBPM(std::string hostname, double bpm) {
    std::string response, contentType;
    std::string url;
    bool status;
    
    // get the pedalboard preset list
    url = "http://" + hostname + "/tempo/set?bpm=" + std::to_string(bpm);
    status = httpPost(url, NULL, 0, &response, &contentType);
    if (!status) {
        std::cout << "setBPM HTTP error: " << response << std::endl;
        return false;
    }
    json_error_t err;
    json_t *root = json_loads(response.c_str(), JSON_DECODE_ANY | JSON_DECODE_INT_AS_REAL, &err);
    if (!root) {
        std::cout << "setBPM: unable to parse JSON" << std::endl;
        return false;
    }
    if (!json_is_boolean(root)) {
        std::cout << "setBPM: root is not a boolean" << std::endl;
        json_decref(root);
        return false;
    }
    json_decref(root);
    return json_boolean_value(root);
}

bool loadPreset(std::string hostname, int preset) {
    std::string response, contentType;
    std::string url;
    bool status;
    
    // get the pedalboard preset list
    url = "http://" + hostname + "/pedalpreset/load?id=" + std::to_string(preset);
    status = httpGet(url, &response, &contentType);
    if (!status) {
        std::cout << "loadPreset HTTP error: " << response << std::endl;
        return false;
    }
    // mod-ui doesn't seem to return anything useful, so assume it went well if we don't
    // get an HTTP error
    return true;
}

bool loadPedalboard(std::string hostname, int pedalboard) {
    if (!modReset(hostname)) {
        std::cout << "loadPedalboard: modReset failed" << std::endl;
        return false;
    }
    
    std::string response, contentType;
    std::string url;
    bool status;
    
    // get the pedalboard preset list
    url = "http://" + hostname + "/pedalboard/load_from_bank/?id=" + std::to_string(pedalboard);
    status = httpPost(url, NULL, 0, &response, &contentType);
    if (!status) {
        std::cout << "loadPedalboard HTTP error: " << response << std::endl;
        return false;
    }
    json_error_t err;
    json_t *root = json_loads(response.c_str(), JSON_DECODE_ANY, &err);
    if (!root) {
        std::cout << "loadPedalboard: unable to parse JSON" << std::endl;
        return false;
    }
    if (!json_is_boolean(root)) {
        std::cout << "loadPedalboard: root is not a boolean" << std::endl;
        json_decref(root);
        return false;
    }
    bool val = json_boolean_value(root);
    json_decref(root);
    return val;
}