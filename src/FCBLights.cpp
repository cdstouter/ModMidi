/* 
 * File:   FCBLights.cpp
 * Author: caleb
 * 
 * Created on December 11, 2017, 11:25 PM
 */

#include "FCBLights.h"

template <typename T>
Light<T>::Light(T initialValue) {
    value = initialValue;
}

template <typename T>
Light<T>::Light(const Light& other) {
    value = other.value;
    dirty = other.dirty;
}

template <typename T>
T Light<T>::getValue() {
    return value;
}

template <typename T>
void Light<T>::setValue(T value) {
    if (value != this->value) {
        this->value = value;
        dirty = true;
    }
}

template <typename T>
bool Light<T>::isDirty() {
    return dirty;
}

template <typename T>
void Light<T>::setDirty(bool dirty) {
    this->dirty = dirty;
}

FCBLights::FCBLights() {
    // set up the lights
    for (int i=0; i<10; i++) {
        pedals.push_back(Light<bool>(false));
    }
    for (int i=0; i<13; i++) {
        miscLights.push_back(Light<bool>(false));
    }
}

FCBLights::~FCBLights() {
}

void FCBLights::setPedal(int pedalNum, bool state) {
    if (pedalNum < 0 || pedalNum >= pedals.size()) return;
    std::lock_guard<std::mutex> guard(m_access);
    pedals.at(pedalNum).setValue(state);
}

void FCBLights::setMiscLight(int lightNum, bool state) {
    if (lightNum < 0 || lightNum >= miscLights.size()) return;
    std::lock_guard<std::mutex> guard(m_access);
    miscLights.at(lightNum).setValue(state);
}

void FCBLights::setDigits(int state) {
    std::lock_guard<std::mutex> guard(m_access);
    digits.setValue(state);
}

void FCBLights::markAllDirty() {
    std::lock_guard<std::mutex> guard(m_access);
    for (auto &i : pedals) {
        i.setDirty(true);
    }
    for (auto &i : miscLights) {
        i.setDirty(true);
    }
    digits.setDirty(true);
}

std::vector<MidiEvent> FCBLights::getMidiEvents() {
    std::lock_guard<std::mutex> guard(m_access);
    std::vector<MidiEvent> events;
    for (int i=0; i<pedals.size(); i++) {
        auto &pedal = pedals.at(i);
        if (pedal.isDirty()) {
            pedal.setDirty(false);
            MidiEvent e;
            e.eventType = MidiEvent::CC;
            e.data1 = pedal.getValue() ? 106 : 107;
            e.data2 = (i == 9) ? 0 : i + 1;
            events.push_back(e);
        }
    }
    for (int i=0; i<miscLights.size(); i++) {
        auto &light = miscLights.at(i);
        if (light.isDirty()) {
            light.setDirty(false);
            // skip 0, 1, and 2 because they get overridden by the FCB itself
            if (i <= 2) continue;
            // skip 5 because we're using it for tap tempo
            if (i == 5) continue;
            MidiEvent e;
            e.eventType = MidiEvent::CC;
            e.data1 = light.getValue() ? 106 : 107;
            e.data2 = i + 11;
            events.push_back(e);
        }
    }
    if (digits.isDirty()) {
        digits.setDirty(false);
        MidiEvent e;
        e.eventType = MidiEvent::CC;
        e.data1 = 108;
        e.data2 = digits.getValue();
        events.push_back(e);
    }
    return events;
}