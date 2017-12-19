/* 
 * File:   Worker.h
 * Author: caleb
 *
 * Created on December 6, 2017, 10:07 AM
 */

#ifndef WORKER_H
#define WORKER_H

#include <mutex>
#include <thread>
#include <jack/jack.h>
#include <deque>
#include <vector>
#include <atomic>

#include "MidiEvent.h"
#include "Utilities.h"
#include "FCBLights.h"

class Worker {
public:
    Worker(jack_client_t *client, jack_port_t *inputPort, jack_port_t* outputPort);
    virtual ~Worker();
    bool start();
    void stop();
    bool midiInput(void* port_buf, jack_nframes_t nframes);
    bool midiOutput(void* port_buf, jack_nframes_t nframes);
    void jackProcess(jack_nframes_t nframes);
    void setHostname(std::string hostname);
    void setSimulate(bool simulate);
    void setDebug(bool debug);
    void setTempoLight(bool tempoLight);
private:
    std::string hostname = "localhost";
    jack_nframes_t nextStatusUpdate = 0;
    std::mutex m_nextStatusUpdate;
    std::deque<MidiEvent> midiInputEvents;
    std::deque<MidiEvent> midiOutputEvents;
    std::mutex m_midiInputEvents, m_midiOutputEvents;
    jack_client_t *client;
    jack_port_t *inputPort, *outputPort;
    void threadWork();
    void statusUpdateThreadWork();
    void processMidi();
    std::thread worker_thread;
    std::thread status_update_thread;
    bool worker_quit = false;
    
    bool needToUpdateLEDS = false;
    std::mutex m_needToUpdateLEDS;
    
    bool loadPreset(unsigned int preset);
    bool loadPedalboard(unsigned int pedalboard);
    void statusUpdate();
    void fcbUpdate();
    void sendNewTempo(double tempo);
    
    // the following variables are all protected by m_status
    std::vector<ModPedalboard> pedalboardList;
    std::vector<std::string> presetList;
    int currentPedalboard = -1;
    int currentPreset = -1;
    int sampleRate = 0;
    unsigned int pedalboardOffset = 0;
    std::mutex m_status;
    
    // the following variables are all protected by m_tapTempo
    bool tapTempoPaused = true; // starts paused, pause is released by initial status update
    bool tapTempoLightOn = false;
    bool tapTempoSendUpdate = false;
    double tapTempoBPM = 0;
    jack_nframes_t tapTempoLength = 0;
    jack_nframes_t tapTempoNextOn = 0, tapTempoNextOff = 0;
    std::deque<double> tapTempoBPMs;
    long int tapTempoLastTime = -1;
    std::mutex m_tapTempo;
    
    void tapTempoPause();
    void tapTempoPlay();
    void tapTempoTap(jack_nframes_t frame, jack_nframes_t nframes);
    void tapTempoProcess(jack_nframes_t nframes);
    void tapTempoSetBPM(double newBPM);
    
    FCBLights fcbLights;
    
    // simulate mode stuff
    bool simulate = false;
    int simulateCurrentPedalboard = 0;
    int simulateCurrentPreset = 0;
    double simulateCurrentBPM = 120.0;
    
    // debugging stuff
    bool debug = false;

    // is the tempo light enabled?
    bool tempoLightEnabled = false;
    
    // socket stuff
    int modSocket1 = -1;
    int modSocket2 = -1;
    std::mutex m_modSocket1, m_modSocket2;
};

#endif /* WORKER_H */

