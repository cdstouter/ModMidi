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

// when debug is true, we pretend to connect to the Mod Duo
static const bool debug = false;
static const bool flashTempo = true;
static int debugCurrentPedalboard = 0;
static int debugCurrentPreset = 0;
static double debugCurrentBPM = 120.0;

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
    worker_thread = std::thread([=] {process();});
}

void Worker::stop() {
    worker_quit = true;
    if (worker_thread.joinable()) worker_thread.join();
}

// called on the jack realtime thread
bool Worker::midiInput(void* port_buf, jack_nframes_t nframes) {
    std::unique_lock<std::mutex> lock(m_midiInputEvents);

    jack_midi_event_t in_event;
    jack_nframes_t event_index = 0;
    jack_nframes_t event_count = jack_midi_get_event_count(port_buf);
    if (event_count == 0) return true;
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
            for (int i=0; i<buf.size(); i++) {
                buffer[i] = buf.at(i);
            }
        }
    }
    return true;
}

void Worker::process() {
    while(!worker_quit) {
        // do midi events need to be processed?
        bool hasInput, needsStatusUpdate;
        {
            std::lock_guard<std::mutex> guard(m_midiInputEvents);
            hasInput = midiInputEvents.size() > 0;
        }
        if (hasInput) processMidi();
        // do we need a status update?
        {
            std::lock_guard<std::mutex> guard(m_nextStatusUpdate);
            needsStatusUpdate = nextStatusUpdate == 0;
        }
        if (needsStatusUpdate) statusUpdate();
        // do we need to send the Mod the new tempo?
        bool hasNewTempo;
        double newTempo;
        {
            std::lock_guard<std::mutex> guard(m_tapTempo);
            hasNewTempo = tapTempoSendUpdate;
            newTempo = tapTempoBPM;
            tapTempoSendUpdate = false;
        }
        if (hasNewTempo) sendNewTempo(newTempo);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    cout << "thread exiting" << endl;
}

void Worker::sendNewTempo(double tempo) {
    if (debug) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::cout << "sent new tempo: " << tempo << std::endl;
    } else {
        setBPM(hostname, tempo);
    }
}

void Worker::processMidi() {
    MidiEvent e;
    while(true) {
        {
            std::lock_guard<std::mutex> guard(m_midiInputEvents);
            if (midiInputEvents.size() == 0) break;
            e = midiInputEvents.front();
            midiInputEvents.pop_front();
        }
        // anything below this line can take as long as it needs
        //e.print();
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

bool Worker::loadPedalboard(int pedalboard) {
    //std::cout << "load pedalboard " << pedalboard << std::endl;
    tapTempoPause();
    {
        std::lock_guard<std::mutex> guard(m_status);
        if (pedalboard < 0 || pedalboard >= pedalboardList.size()) return false;
    }
    if (debug) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        debugCurrentPedalboard = pedalboard;
        debugCurrentPreset = 0;
        debugCurrentBPM = 120;
        return true;
    } else {
        std::lock_guard<std::mutex> guard(m_status);
        return ::loadPedalboard(hostname, pedalboard);
    }
}

bool Worker::loadPreset(int preset) {
    {
        std::lock_guard<std::mutex> guard(m_status);
        if (preset < 0 || preset >= presetList.size()) return false;
    }
    if (debug) {
        debugCurrentPreset = preset;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::lock_guard<std::mutex> guard(m_status);
        currentPreset = debugCurrentPreset;
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
    
    if (debug) {
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
    if (!status) {
        std::cout << "Error getting current bank" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(2));
        return;
    } else {
        std::lock_guard<std::mutex> guard(m_status);
        std::cout << "Current bank:" << std::endl;
        for (int i=0; i<pedalboardList.size(); i++) {
            std::cout << std::to_string(i) << ": " << pedalboardList.at(i).title << std::endl;
        }
        if (pedalboardList.size() == 0) {
            std::cout << "Current bank is empty." << std::endl;
        }
    }

    if (debug) {
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
    if (!status) {
        std::cout << "Error getting preset list" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(2));
        return;
    } else {
        std::lock_guard<std::mutex> guard(m_status);
        std::cout << "Preset list:" << std::endl;
        for (int i=0; i<presetList.size(); i++) {
            std::cout << std::to_string(i) << ": " << presetList.at(i) << std::endl;
        }
        if (presetList.size() == 0) {
            std::cout << "Current patch has no presets." << std::endl;
        }
    }
    
    if (debug) {
        status = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        std::lock_guard<std::mutex> guard(m_status);
        currentPedalboard = debugCurrentPedalboard;
        currentPreset = debugCurrentPreset;
    } else {
        status = getCurrentPedalboardAndPreset(hostname, pedalboardList, currentPedalboard, currentPreset, &m_status);
    }
    if (!status) {
        std::cout << "Error getting current pedalboard & preset" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(2));
        return;
    } else {
        std::lock_guard<std::mutex> guard(m_status);
        // make sure the pedalboard offset is within range
        if (pedalboardOffset < 0 || pedalboardOffset >= pedalboardList.size()) {
            if (currentPedalboard < 0) {
                pedalboardOffset = 0;
            } else {
                pedalboardOffset = (currentPedalboard / 5) * 5;
            }
        }
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
    if (debug) {
        status = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        bpm = debugCurrentBPM;
    } else {
        status = getCurrentBPM(hostname, bpm, NULL);
    }
    if (!status) {
        std::cout << "Error getting current BPM" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(2));
        return;
    } else {
        tapTempoSetBPM(bpm);
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
        fcbLights.setPedal(i + 5, (i + pedalboardOffset) == currentPedalboard);
    }
    for (int i=0; i<13; i++) {
        fcbLights.setMiscLight(i, false);
    }
    if (currentPedalboard >= pedalboardOffset && (currentPedalboard - pedalboardOffset) < 5) {
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
    //std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::lock_guard<std::mutex> guard(m_tapTempo);
    tapTempoPaused = false;
    tapTempoLastTime = -1;
    tapTempoBPMs.clear();
}

// called from the jack realtime thread with a lock already on midiOutputEvents
void Worker::tapTempoProcess(jack_nframes_t nframes) {
    std::lock_guard<std::mutex> guard(m_tapTempo);
    if (tapTempoPaused || tapTempoLength == 0) {
        if (tapTempoLightOn) {
            // turn it off
            if (flashTempo) {
                MidiEvent e;
                e.eventType = MidiEvent::CC;
                e.data1 = 107;
                e.data2 = 16;
                midiOutputEvents.push_back(e);
                tapTempoLightOn = false;
            }
        }
        return;
    }
    if (tapTempoNextOn < nframes) {
        if (flashTempo) {
            MidiEvent e;
            e.eventType = MidiEvent::CC;
            e.data1 = 106;
            e.data2 = 16;
            e.time = tapTempoNextOn;
            midiOutputEvents.push_back(e);
        }
        tapTempoLightOn = true;
        tapTempoNextOn += tapTempoLength;
    } else {
        tapTempoNextOn -= nframes;
    }
    if (tapTempoNextOff < nframes) {
        if (flashTempo) {
            MidiEvent e;
            e.eventType = MidiEvent::CC;
            e.data1 = 107;
            e.data2 = 16;
            e.time = tapTempoNextOff;
            midiOutputEvents.push_back(e);
        }
        tapTempoLightOn = false;
        tapTempoNextOff += tapTempoLength;
    } else {
        tapTempoNextOff -= nframes;
    }
    if (tapTempoLastTime >= 0) tapTempoLastTime += nframes;
    if (tapTempoLastTime > (sampleRate * 2)) {
        tapTempoLastTime = -1;
        tapTempoBPMs.clear();
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