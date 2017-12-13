/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   main.cpp
 * Author: caleb
 *
 * Created on December 5, 2017, 10:51 PM
 */

#include <cstdlib>
#include <iostream>
#include <csignal>
#include <chrono>
#include <unistd.h>
#include <jack/jack.h>
#include <jack/midiport.h>

#include "Worker.h"

using namespace std;

jack_client_t *client = NULL;
jack_port_t *input_port, *output_port;

Worker *worker = NULL;

static void signal_handler(int sig) {
    cerr << "signal received, exiting ..." << endl;
    if (client != NULL) {
        cout << "shutting down jack client..." << endl;
        jack_client_close(client);
    }
    if (worker) {
        worker->stop();
        delete worker;
    }
    exit(0);
}

// Jack process callback
static int process(jack_nframes_t nframes, void *arg) {
    if (!worker) return 0;
    // give MIDI input to the worker
    void* port_buf = jack_port_get_buffer(input_port, nframes);
    worker->midiInput(port_buf, nframes);
    // call the worker's process function
    worker->jackProcess(nframes);
    // get MIDI output from the worker
    port_buf = jack_port_get_buffer(output_port, nframes);
    worker->midiOutput(port_buf, nframes);
    return 0;
}

// utility function to find a jack port
std::string getPort(std::string regEx, unsigned long flags) {
    const char **ports;
    ports = jack_get_ports(client, regEx.c_str(), JACK_DEFAULT_MIDI_TYPE, flags);
    if (!ports) return "";

    const char *port = ports[0];
    std::string retVal = "";
    if (port) {
        retVal = port;
    }
    jack_free(ports);
    return retVal;
}

// attempt to connect the ports
bool connectPorts(std::string inputPortRegEx, std::string outputPortRegEx) {
    std::cout << "attempting to connect ports..." << std::endl;
    if (!jack_port_connected(input_port)) {
        std::string inputPortName = getPort(inputPortRegEx, JackPortIsOutput);
        if (inputPortName.length() > 0) {
            int retval = jack_connect(client, inputPortName.c_str(), jack_port_name(input_port));
            if (retval == 0) {
                std::cout << "connected " << inputPortName << " to input port" << std::endl;
            }
        }
    }
    if (!jack_port_connected(output_port)) {
        std::string outputPortName = getPort(outputPortRegEx, JackPortIsInput);
        if (outputPortName.length() > 0) {
            int retval = jack_connect(client, jack_port_name(output_port), outputPortName.c_str());
            if (retval == 0) {
                std::cout << "connected output port to " << outputPortName << std::endl;
            }
        }
    }
    if (!jack_port_connected(input_port) || !jack_port_connected(output_port)) return false;
    return true;
}

int main(int argc, char** argv) {
    
    // set up signal handling
    signal(SIGQUIT, signal_handler);
    signal(SIGHUP, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    
    // start up our Jack client
    client = jack_client_open("ModMidi", JackNoStartServer, NULL);
    if (!client) {
        cout << "unable to connect to jack server" << endl;
        return -1;
    }

    cout << "starting program..." << endl;
    
    jack_set_process_callback(client, process, 0);
    input_port = jack_port_register(client, "input", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
    output_port = jack_port_register(client, "output", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
    if (jack_activate(client)) {
        cout << "unable to activate jack client" << endl;
        jack_client_close(client);
        return -1;
    }
    // connect the ports
    //bool connected = connectPorts("UMC204", "UMC204"); // desktop UMC204
    bool connected = connectPorts("ttymidi:MIDI_in", "ttymidi:MIDI_out"); // Mod Duo
    if (!connected) {
        cout << "unable to connect ports" << endl;
        jack_client_close(client);
        return -1;
    }
    Worker *workerTemp = new Worker(client, input_port, output_port);
    //workerTemp->setHostname("modduo.local"); // desktop testing
    workerTemp->start();
    worker = workerTemp;
    workerTemp = NULL;
    sleep(-1);
    worker->stop();
    delete worker;
    
    if (client != NULL) {
        cout << "shutting down jack client..." << endl;
        jack_client_close(client);
    }

    return 0;
}

