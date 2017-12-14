/* 
 * File:   MidiEvent.h
 * Author: caleb
 *
 * Created on December 9, 2017, 11:28 AM
 */

#ifndef MIDIEVENT_H
#define MIDIEVENT_H

#include <vector>

#include <jack/jack.h>
#include <jack/midiport.h>

class MidiEvent {
public:
    MidiEvent();
    MidiEvent(const MidiEvent& orig);
    MidiEvent(jack_midi_event_t jack_midi_event);
    virtual ~MidiEvent();
    void print();
    std::vector<unsigned char> getBuffer();
    static bool comparator(const MidiEvent &a, const MidiEvent &b);
    
    unsigned char channel = 0;
    
    enum EventType {
        NOTE_OFF,
        NOTE_ON,
        POLYPHONIC_AFTERTOUCH,
        CC,
        PROGRAM_CHANGE,
        CHANNEL_AFTERTOUCH,
        PITCH_WHEEL,
        OTHER
    };
    
    EventType eventType = EventType::OTHER;
    
    unsigned char data1 = 0, data2 = 0;
    
    jack_nframes_t time = 0;
};

#endif /* MIDIEVENT_H */

