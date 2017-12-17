/* 
 * File:   Worker.cpp
 * Author: caleb
 * 
 * Created on December 6, 2017, 10:07 AM
 */

#include "Worker.h"

#include <iostream>
#include <string.h>
#include <jansson.h>

#include <jack/midiport.h>
#include <valarray>

using namespace std;

void Worker::setDebug(bool debug) {
    this->debug = debug;
}

void Worker::setSimulate(bool simulate) {
    this->simulate = simulate;
}

void Worker::setTempoLight(bool tempoLight) {
    tempoLightEnabled = tempoLight;
}

Worker::Worker(jack_client_t *client, jack_port_t *inputPort, jack_port_t *outputPort) {
    this->client = client;
    this->inputPort = inputPort;
    this->outputPort = outputPort;
    
    sampleRate = jack_get_sample_rate(client);
}

Worker::~Worker() {
    stop();
}

void Worker::start() {
    worker_quit = false;
    worker_thread = std::thread([=] {threadWork();});
    status_update_thread = std::thread([=] {statusUpdateThreadWork();});
}

void Worker::stop() {
    worker_quit = true;
    if (worker_thread.joinable()) worker_thread.join();
    if (status_update_thread.joinable()) status_update_thread.join();
}

// called on the jack realtime thread
bool Worker::midiInput(void* port_buf, jack_nframes_t nframes) {
    jack_midi_event_t in_event;
    jack_nframes_t event_count = jack_midi_get_event_count(port_buf);
    if (event_count == 0) return true;

    std::unique_lock<std::mutex> lock(m_midiInputEvents);
    for (jack_nframes_t i=0; i<event_count; i++) {
        jack_midi_event_get(&in_event, port_buf, i);
        // filter out tap tempo button events & deal with them directly
        MidiEvent e(in_event);
        if (e.eventType == MidiEvent::CC && e.data1 == 104 && e.data2 == 11) {
            tapTempoTap(e.time, nframes);
        } else {
            midiInputEvents.push_back(e);
        }
    }
    return true;
}

// called on the jack realtime thread
bool Worker::midiOutput(void* port_buf, jack_nframes_t nframes) {
    std::unique_lock<std::mutex> lock(m_midiOutputEvents);
    
    jack_midi_clear_buffer(port_buf);
    
    while(midiOutputEvents.size() > 0) {
        MidiEvent e = midiOutputEvents.front();
        midiOutputEvents.pop_front();
        std::vector<unsigned char> buf = e.getBuffer();
        if (buf.size()) {
            unsigned char* buffer = jack_midi_event_reserve(port_buf, e.time, buf.size());
            for (size_t i=0; i<buf.size(); i++) {
                buffer[i] = buf.at(i);
            }
        }
    }
    return true;
}

void Worker::statusUpdateThreadWork() {
    // variable used in the status update thread loop
    bool needsStatusUpdate;
    // start the loop
    while(!worker_quit) {
        // do we need a status update?
        {
            std::lock_guard<std::mutex> guard(m_nextStatusUpdate);
            needsStatusUpdate = nextStatusUpdate == 0;
        }
        if (needsStatusUpdate) statusUpdate();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    cout << "Status update thread exiting" << endl;
}

void Worker::threadWork() {
    // variables used in the worker loop
    bool hasNewTempo;
    double newTempo;
    // start the loop
    while(!worker_quit) {
        // do midi events need to be processed?
        processMidi();
        // do we need to send the Mod the new tempo?
        {
            std::lock_guard<std::mutex> guard(m_tapTempo);
            hasNewTempo = tapTempoSendUpdate;
            newTempo = tapTempoBPM;
            tapTempoSendUpdate = false;
        }
        if (hasNewTempo) sendNewTempo(newTempo);
        std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    cout << "Thread exiting" << endl;
}

void Worker::sendNewTempo(double tempo) {
    if (simulate) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::cout << "sent new tempo: " << tempo << std::endl;
    } else {
        setBPM(hostname, tempo);
    }
}

void Worker::processMidi() {
    MidiEvent e;
    std::deque<MidiEvent> events;
    while(true) {
        {
            std::lock_guard<std::mutex> guard(m_midiInputEvents);
            if (midiInputEvents.size() == 0) break;
            events = midiInputEvents;
            midiInputEvents.clear();
        }
        // anything below this line can take as long as it needs
        for (auto &e : events) {
            bool updateLights = false, updateStatus = false;
            if (e.eventType == MidiEvent::CC && e.data1 == 104) {
                // bank up button pressed
                if (e.data2 == 10) {
                    std::lock_guard<std::mutex> guard(m_status);
                    pedalboardOffset += 5;
                    if (pedalboardOffset >= pedalboardList.size()) pedalboardOffset = 0;
                    updateLights = true;
                }
                // preset button pressed
                if (e.data2 >= 1 && e.data2 <= 5) {
                    updateLights = loadPreset(e.data2 - 1);
                }
                // pedalboard button pressed
                if ((e.data2 >= 6 && e.data2 <= 9) || e.data2 == 0) {
                    int pedalboard = e.data2 > 0 ? e.data2 - 6 : 4;
                    {
                        std::lock_guard<std::mutex> guard(m_status);
                        pedalboard += pedalboardOffset;
                    }
                    updateStatus = loadPedalboard(pedalboard);
                }
            }
            if (updateStatus) {
                statusUpdate();
            } else if (updateLights) {
                fcbUpdate();
            }
        }
    }
}

// called from jack's realtime thread
void Worker::jackProcess(jack_nframes_t nframes) {
    std::lock_guard<std::mutex> guard(m_midiOutputEvents);
    tapTempoProcess(nframes);
    auto temp = fcbLights.getMidiEvents();
    midiOutputEvents.insert(midiOutputEvents.end(), temp.begin(), temp.end());
    {
        std::lock_guard<std::mutex> guard(m_nextStatusUpdate);
        if (nextStatusUpdate < nframes) {
            nextStatusUpdate = 0;
        } else {
            nextStatusUpdate -= nframes;
        }
    }
}

void Worker::setHostname(std::string hostname) {
    this->hostname = hostname;
}

bool Worker::loadPedalboard(unsigned int pedalboard) {
    //std::cout << "load pedalboard " << pedalboard << std::endl;
    tapTempoPause();
    {
        std::lock_guard<std::mutex> guard(m_status);
        if (pedalboard >= pedalboardList.size()) return false;
    }
    if (simulate) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        simulateCurrentPedalboard = pedalboard;
        simulateCurrentPreset = 0;
        simulateCurrentBPM = 120;
        return true;
    } else {
        std::lock_guard<std::mutex> guard(m_status);
        return ::loadPedalboard(hostname, pedalboard);
    }
}

bool Worker::loadPreset(unsigned int preset) {
    {
        std::lock_guard<std::mutex> guard(m_status);
        if (preset >= presetList.size()) return false;
    }
    if (simulate) {
        simulateCurrentPreset = preset;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::lock_guard<std::mutex> guard(m_status);
        currentPreset = simulateCurrentPreset;
        return true;
    } else {
        if (!::loadPreset(hostname, preset)) return false;
        std::lock_guard<std::mutex> guard(m_status);
        currentPreset = preset;
        return true;
    }
}

void Worker::statusUpdate() {
    std::string response, contentType;
    std::string url;
    bool status;
    
    if (simulate) {
        status = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::lock_guard<std::mutex> guard(m_status);
        pedalboardList.clear();
        ModPedalboard p;
        for (int i=0; i<8; i++) {
            p.title = "Patch 1";
            p.bundle = "patch1";
            pedalboardList.push_back(p);
            p.title = "Patch 2";
            p.bundle = "patch2";
            pedalboardList.push_back(p);
        }
    } else {
        status = getPedalboardList(hostname, pedalboardList, &m_status);
    }
    if (!status) std::cout << "Error getting current bank" << std::endl;
    if (debug) {
        std::lock_guard<std::mutex> guard(m_status);
        std::cout << "Current bank:" << std::endl;
        for (size_t i=0; i<pedalboardList.size(); i++) {
            std::cout << std::to_string(i) << ": " << pedalboardList.at(i).title << std::endl;
        }
        if (pedalboardList.size() == 0) {
            std::cout << "Current bank is empty." << std::endl;
        }
    }

    if (simulate) {
        status = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        std::lock_guard<std::mutex> guard(m_status);
        presetList.clear();
        presetList.push_back("clean");
        presetList.push_back("OD");
        presetList.push_back("solo");
    } else {
        status = getPresetList(hostname, presetList, &m_status);
    }
    if (!status) std::cout << "Error getting preset list" << std::endl;
    if (debug) {
        std::lock_guard<std::mutex> guard(m_status);
        std::cout << "Preset list:" << std::endl;
        for (size_t i=0; i<presetList.size(); i++) {
            std::cout << std::to_string(i) << ": " << presetList.at(i) << std::endl;
        }
        if (presetList.size() == 0) {
            std::cout << "Current patch has no presets." << std::endl;
        }
    }
    
    if (simulate) {
        status = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        std::lock_guard<std::mutex> guard(m_status);
        currentPedalboard = simulateCurrentPedalboard;
        currentPreset = simulateCurrentPreset;
    } else {
        status = getCurrentPedalboardAndPreset(hostname, pedalboardList, currentPedalboard, currentPreset, pedalboardOffset, &m_status);
    }
    if (!status) std::cout << "Error getting current pedalboard & preset" << std::endl;

    if (debug) {
        std::cout << "Current pedalboard: " << std::to_string(currentPedalboard) << std::endl;
        if (currentPedalboard >= 0) {
            std::cout << pedalboardList.at(currentPedalboard).title << std::endl;
        } else {
            std::cout << "None" << std::endl;
        }
        std::cout << "Current preset: " << std::to_string(currentPreset) << std::endl;
        if (currentPreset >= 0) {
            std::cout << presetList.at(currentPreset) << std::endl;
        } else {
            std::cout << "None" << std::endl;
        }
    }
    
    double bpm;
    if (simulate) {
        status = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        bpm = simulateCurrentBPM;
    } else {
        status = getCurrentBPM(hostname, bpm, NULL);
    }
    if (status) {
        if (debug) {
            std::cout << "Current BPM: " << bpm << std::endl;
        }
        tapTempoSetBPM(bpm);
    } else {
        std::cout << "Error getting current BPM" << std::endl;
    }

    {
        std::lock_guard<std::mutex> guard(m_nextStatusUpdate);
        nextStatusUpdate = sampleRate * 10;
    }
    fcbUpdate();
    tapTempoPlay();
}

void Worker::fcbUpdate() {
    std::lock_guard<std::mutex> guard(m_status);
    // update the LEDs
    for (int i=0; i<5; i++) {
        fcbLights.setPedal(i, i == currentPreset);
        fcbLights.setPedal(i + 5, (i + (int)pedalboardOffset) == currentPedalboard);
    }
    for (int i=0; i<13; i++) {
        fcbLights.setMiscLight(i, false);
    }
    if (currentPedalboard >= (int)pedalboardOffset && (currentPedalboard - (int)pedalboardOffset) < 5) {
        fcbLights.setMiscLight(12, false);
        fcbLights.setDigits(currentPedalboard + 1);
    } else {
        fcbLights.setMiscLight(12, true);
        fcbLights.setDigits(pedalboardOffset + 1);
    }
}

void Worker::tapTempoPause() {
    std::lock_guard<std::mutex> guard(m_tapTempo);
    tapTempoPaused = true;
    tapTempoLastTime = -1;
    tapTempoBPMs.clear();
}

void Worker::tapTempoPlay() {
    std::lock_guard<std::mutex> guard(m_tapTempo);
    tapTempoPaused = false;
    tapTempoLastTime = -1;
    tapTempoBPMs.clear();
}

// called from the jack realtime thread with a lock already on midiOutputEvents
void Worker::tapTempoProcess(jack_nframes_t nframes) {
    std::lock_guard<std::mutex> guard(m_tapTempo);
    if (tapTempoLastTime >= 0) tapTempoLastTime += nframes;
    if (tapTempoLastTime > (sampleRate * 2)) {
        tapTempoLastTime = -1;
        tapTempoBPMs.clear();
    }
    if (!tempoLightEnabled) return;
    // everything below this line only runs if the tempo light is enabled
    if (tapTempoPaused || tapTempoLength == 0) {
        if (tapTempoLightOn) {
            // turn it off
            MidiEvent e;
            e.eventType = MidiEvent::CC;
            e.data1 = 107;
            e.data2 = 16;
            midiOutputEvents.push_back(e);
            tapTempoLightOn = false;
        }
        return;
    }
    if (tapTempoNextOn < nframes) {
        MidiEvent e;
        e.eventType = MidiEvent::CC;
        e.data1 = 106;
        e.data2 = 16;
        e.time = tapTempoNextOn;
        midiOutputEvents.push_back(e);
        tapTempoLightOn = true;
        tapTempoNextOn += tapTempoLength;
    } else {
        tapTempoNextOn -= nframes;
    }
    if (tapTempoNextOff < nframes) {
        MidiEvent e;
        e.eventType = MidiEvent::CC;
        e.data1 = 107;
        e.data2 = 16;
        e.time = tapTempoNextOff;
        midiOutputEvents.push_back(e);
        tapTempoLightOn = false;
        tapTempoNextOff += tapTempoLength;
    } else {
        tapTempoNextOff -= nframes;
    }
}

void Worker::tapTempoSetBPM(double newBPM) {
    std::lock_guard<std::mutex> guard(m_tapTempo);
    tapTempoBPM = newBPM;
    tapTempoLength = ((double)sampleRate * 60.0) / tapTempoBPM;
    tapTempoNextOn = 0;
    tapTempoNextOff = tapTempoLength / 4;
}

// called from the jack realtime thread
void Worker::tapTempoTap(jack_nframes_t frame, jack_nframes_t nframes) {
    std::lock_guard<std::mutex> guard(m_tapTempo);
    if (tapTempoPaused) return;
    if (tapTempoLastTime >= 0) {
        long int totalTime = tapTempoLastTime - (nframes - frame);
        double bpm = ((double)sampleRate * 60.0) / (double)totalTime;
        tapTempoBPMs.push_back(bpm);
        while(tapTempoBPMs.size() > 4) {
            tapTempoBPMs.pop_front();
        }
        double averageBPM = 0;
        for (double &bpm : tapTempoBPMs) {
            averageBPM += bpm;
        }
        averageBPM = averageBPM / (double)tapTempoBPMs.size();
        // set the new BPM
        if (std::abs(averageBPM - tapTempoBPM) > .01) {
            // we need to notify the Mod Duo of the new tempo
            tapTempoSendUpdate = true;
        }
        tapTempoBPM = averageBPM;
        tapTempoLength = ((double)sampleRate * 60.0) / tapTempoBPM;
    }
    tapTempoLastTime = nframes - frame;
    tapTempoNextOn = 0;
    tapTempoNextOff = tapTempoLength / 4;
}