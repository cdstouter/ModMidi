/* 
 * File:   MidiEvent.cpp
 * Author: caleb
 * 
 * Created on December 9, 2017, 11:28 AM
 */

#include "MidiEvent.h"

#include <iostream>
#include <jack/midiport.h>

MidiEvent::MidiEvent() {
}

MidiEvent::MidiEvent(const MidiEvent& orig) {
    this->channel = orig.channel;
    this->data1 = orig.data1;
    this->data2 = orig.data2;
    this->eventType = orig.eventType;
}

MidiEvent::~MidiEvent() {
}

MidiEvent::MidiEvent(jack_midi_event_t jack_midi_event) {
    time = jack_midi_event.time;
    channel = data1 = data2 = 0;
    eventType = EventType::OTHER;
    channel = jack_midi_event.buffer[0] & 0x0F;
    unsigned char type = jack_midi_event.buffer[0] & 0xF0;
    switch(type) {
        case 0x80:
            eventType = EventType::NOTE_OFF;
            data1 = jack_midi_event.buffer[1];
            data2 = jack_midi_event.buffer[2];
            break;
        case 0x90:
            eventType = EventType::NOTE_ON;
            data1 = jack_midi_event.buffer[1];
            data2 = jack_midi_event.buffer[2];
            break;
        case 0xA0:
            eventType = EventType::POLYPHONIC_AFTERTOUCH;
            data1 = jack_midi_event.buffer[1];
            data2 = jack_midi_event.buffer[2];
            break;
        case 0xB0:
            eventType = EventType::CC;
            data1 = jack_midi_event.buffer[1];
            data2 = jack_midi_event.buffer[2];
            break;
        case 0xC0:
            eventType = EventType::PROGRAM_CHANGE;
            data1 = jack_midi_event.buffer[1];
            break;
        case 0xD0:
            eventType = EventType::CHANNEL_AFTERTOUCH;
            data1 = jack_midi_event.buffer[1];
            break;
        case 0xE0:
            eventType = EventType::PITCH_WHEEL;
            data1 = jack_midi_event.buffer[1];
            data2 = jack_midi_event.buffer[2];
            break;
        case 0xF0:
        default:
            eventType = EventType::OTHER;
    }
}

std::vector<unsigned char> MidiEvent::getBuffer() {
    std::vector<unsigned char> buf;
    switch(eventType) {
        case EventType::CC:
            buf.push_back(0xB0 + channel);
            buf.push_back(data1);
            buf.push_back(data2);
            return buf;
        case EventType::CHANNEL_AFTERTOUCH:
            buf.push_back(0xD0 + channel);
            buf.push_back(data1);
            return buf;
        case EventType::NOTE_OFF:
            buf.push_back(0x80 + channel);
            buf.push_back(data1);
            buf.push_back(data2);
            return buf;
        case EventType::NOTE_ON:
            buf.push_back(0x90 + channel);
            buf.push_back(data1);
            buf.push_back(data2);
            return buf;
        case EventType::PITCH_WHEEL:
            buf.push_back(0xE0 + channel);
            buf.push_back(data1);
            buf.push_back(data2);
            return buf;
        case EventType::POLYPHONIC_AFTERTOUCH:
            buf.push_back(0xA0 + channel);
            buf.push_back(data1);
            buf.push_back(data2);
            return buf;
        case EventType::PROGRAM_CHANGE:
            buf.push_back(0xC0 + channel);
            buf.push_back(data1);
            return buf;
        case EventType::OTHER:
        default:
            // return an empty buffer since we don't know how to deal with these types
            return buf;
    }
}

void MidiEvent::print() {
    std::cout << "Midi event: ";
    switch(eventType) {
        case EventType::CC:
            std::cout << "CC";
            break;
        case EventType::CHANNEL_AFTERTOUCH:
            std::cout << "channel aftertouch";
            break;
        case EventType::NOTE_OFF:
            std::cout << "note off";
            break;
        case EventType::NOTE_ON:
            std::cout << "note on";
            break;
        case EventType::PITCH_WHEEL:
            std::cout << "pitch wheel";
            break;
        case EventType::POLYPHONIC_AFTERTOUCH:
            std::cout << "polyphonic aftertouch";
            break;
        case EventType::PROGRAM_CHANGE:
            std::cout << "program change";
            break;
        case EventType::OTHER:
        default:
            std::cout << "other";
    }
    std::cout << " on channel " << (int)channel << ": " << (int)data1 << " " << (int)data2 << std::endl;
}