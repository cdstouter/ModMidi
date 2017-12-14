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
#include <getopt.h>

#include "Worker.h"

using namespace std;

jack_client_t *client = NULL;
// these get connected to the Mod's serial MIDI input & output
jack_port_t *input_port, *output_port;
// this gets connected to mod-host to send CCs
jack_port_t *mod_cc_port;

Worker *worker = NULL;

static void signal_handler(int sig) {
    cerr << "Signal received, exiting ..." << endl;
    if (client != NULL) {
        cout << "Shutting down jack client..." << endl;
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
    // call the worker's process function
    void* input_port_buf = jack_port_get_buffer(input_port, nframes);
    void* output_port_buf = jack_port_get_buffer(output_port, nframes);
    void* cc_port_buf = jack_port_get_buffer(mod_cc_port, nframes);
    worker->jackProcess(input_port_buf, output_port_buf, cc_port_buf, nframes);
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
    std::cout << "Attempting to connect ports..." << std::endl;
    jack_port_t *inputPort = NULL;
    std::string inputPortName;
    if (!jack_port_connected(input_port)) {
        inputPortName = getPort(inputPortRegEx, JackPortIsOutput);
        if (inputPortName.length() > 0) {
            inputPort = jack_port_by_name(client, inputPortName.c_str());
            int retval = jack_connect(client, inputPortName.c_str(), jack_port_name(input_port));
            if (retval == 0) {
                std::cout << "Connected " << inputPortName << " to input port" << std::endl;
            } else {
                std::cout << "connecting " << inputPortName << " to input port failed" << std::endl;
                return false;
            }
        } else {
            std::cout << "Unable to find port matching " << inputPortRegEx << std::endl;
            return false;
        }
    }
    if (!jack_port_connected(output_port)) {
        std::string outputPortName = getPort(outputPortRegEx, JackPortIsInput);
        if (outputPortName.length() > 0) {
            int retval = jack_connect(client, jack_port_name(output_port), outputPortName.c_str());
            if (retval == 0) {
                std::cout << "Connected output port to " << outputPortName << std::endl;
            } else {
                std::cout << "Connecting " << outputPortName << " to output port failed" << std::endl;
                return false;
            }
        } else {
            std::cout << "Unable to find port matching " << inputPortRegEx << std::endl;
            return false;
        }
    }
    // disconnect our input port from mod-host
    if (!inputPort) {
        std::cout << "This shouldn't happen, inputPort doesn't exist" << std::endl;
        return false;
    }
    const char** ports = jack_port_get_all_connections(client, inputPort);
    if (ports) {
        int i=0;
        while(ports[i] != NULL) {
            std::string portName(ports[i]);
            if (portName == "mod-host:midi_in") {
                std::cout << "Input port is connected to mod-host:midi_in, disconnecting" << std::endl;
                if (jack_disconnect(client, inputPortName.c_str(), "mod-host:midi_in") != 0) {
                    std::cout << "Unable to disconnect input port from mod-host:midi_in" << std::endl;
                    return false;
                }
                break;
            }
            i++;
        }
        jack_free(ports);
    }
    // connect our CC port to Mod
    std::string ccPortName = "mod-host:midi_in";
    //std::string ccPortName = "midi-monitor:input";
    if (!jack_port_connected(mod_cc_port)) {
        std::string modMidiInPortName = getPort(ccPortName, JackPortIsInput);
        if (modMidiInPortName.length() > 0) {
            int retval = jack_connect(client, jack_port_name(mod_cc_port), modMidiInPortName.c_str());
            if (retval == 0) {
                std::cout << "Connected CC port to " << modMidiInPortName << std::endl;
            } else {
                std::cout << "Connecting " << modMidiInPortName << " to CC port failed" << std::endl;
                return false;
            }
        } else {
            std::cout << "Unable to find port matching " << ccPortName << std::endl;
            return false;
        }
    }
    //if (!jack_port_connected(input_port) || !jack_port_connected(output_port)) return false;
    if (!jack_port_connected(mod_cc_port)) return false;
    return true;
}

int main(int argc, char** argv) {
    
    // try getopt
    static struct option long_options[] = {
        {"help", no_argument, NULL, 'h'},
        {"hostname", required_argument, NULL, 'n'},
        {"input", required_argument, NULL, 'i'},
        {"output", required_argument, NULL, 'o'},
        {"flash", no_argument, NULL, 'f'},
        {"debug", no_argument, NULL, 'd'},
        {"simulate", no_argument, NULL, 's'},
        {0, 0, 0, 0}
    };
    
    int option_index = 0;
    int c;
    bool optionHelp = false;
    bool optionFlash = false;
    bool parseError = false;
    bool optionDebug = false;
    bool optionSimulate = false;
    std::string optionHostname, optionInput, optionOutput;
    while ((c = getopt_long(argc, argv, "hn:i:o:f", long_options, &option_index)) != -1) {
        switch(c) {
            case 'h':
                optionHelp = true;
                break;
            case 'n':
                optionHostname = std::string(optarg);
                break;
            case 'i':
                optionInput = std::string(optarg);
                break;
            case 'o':
                optionOutput = std::string(optarg);
                break;
            case 'f':
                optionFlash = true;
                break;
            case 'd':
                optionDebug = true;
                break;
            case 's':
                optionSimulate = true;
            case '?':
                optionHelp = true;
                parseError = true;
                break;
        }
    }
    if (optind < argc) {
        std::cout << "Unexpected arguments found." << std::endl;
        optionHelp = true;
        parseError = true;
    }
    
    if (optionHelp) {
        // display help information
        std::cout << std::endl;
        std::cout << "ModMidi command line options:" << std::endl << std::endl;
        std::cout << "    -h, --help           display this help information" << std::endl;
        std::cout << "    -n, --hostname HOST  set the hostname of the Mod Duo" << std::endl;
        std::cout << "    -i, --input PORT     jack midi input port to use (regex)" << std::endl;
        std::cout << "    -o, --output PORT    jack midi output port to use (regex)" << std::endl;
        std::cout << "    -f, --flash          enable flashing tempo light" << std::endl;
        std::cout << "    -d, --debug          print some debugging information" << std::endl;
        std::cout << "    -s, --simulate       pretend to connect to the Mod" << std::endl;
        return parseError ? -1 : 0;
    }
    
    // set up signal handling
    signal(SIGQUIT, signal_handler);
    signal(SIGHUP, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    
    cout << "Starting ModMidi..." << endl;
    
    // start up our Jack client
    client = jack_client_open("ModMidi", JackNoStartServer, NULL);
    if (!client) {
        cout << "Unable to connect to jack server" << endl;
        return -1;
    }

    jack_set_process_callback(client, process, 0);
    input_port = jack_port_register(client, "input", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
    output_port = jack_port_register(client, "output", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput | JackPortIsTerminal, 0);
    mod_cc_port = jack_port_register(client, "mod_cc_output", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput | JackPortIsTerminal, 0);
    if (jack_activate(client)) {
        cout << "Unable to activate jack client" << endl;
        jack_client_close(client);
        return -1;
    }
    // connect the ports
    string inputPort = optionInput.size() > 0 ? optionInput : "ttymidi:MIDI_in";
    string outputPort = optionOutput.size() > 0 ? optionOutput : "ttymidi:MIDI_out";
    cout << "Port names we're connecting to:" << endl << "  " << inputPort << endl << "  " << outputPort << endl;
    bool connected = connectPorts(inputPort, outputPort);
    if (!connected) {
        cout << "Unable to connect ports." << endl;
        jack_client_close(client);
        return -1;
    }
    Worker *workerTemp = new Worker(client, input_port, output_port);
    if (optionHostname.size() > 0) {
        cout << "Using Mod Duo hostname: " << optionHostname << endl;
        workerTemp->setHostname(optionHostname);
    } else {
        cout << "Using Mod Duo hostname: localhost" << endl;
    }
    workerTemp->setSimulate(optionSimulate);
    workerTemp->setDebug(optionDebug);
    workerTemp->setTempoLight(optionFlash);
    workerTemp->start();
    worker = workerTemp;
    workerTemp = NULL;
    sleep(-1);
    worker->stop();
    delete worker;
    
    if (client != NULL) {
        cout << "Shutting down jack client..." << endl;
        jack_client_close(client);
    }

    return 0;
}

