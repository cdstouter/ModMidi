/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   FCBLights.h
 * Author: caleb
 *
 * Created on December 11, 2017, 11:25 PM
 */

#ifndef FCBLIGHTS_H
#define FCBLIGHTS_H

#include <mutex>
#include <vector>

#include "MidiEvent.h"

template <typename T>
class Light {
public:
    Light(T initialValue);
    Light(const Light& other);
    T getValue();
    void setValue(T value);
    bool isDirty();
    void setDirty(bool dirty);
private:
    T value;
    bool dirty = true;
};

// this class is thread safe
class FCBLights {
public:
    FCBLights();
    virtual ~FCBLights();
    
    void setPedal(int pedalNum, bool state);
    void setMiscLight(int lightNum, bool state);
    void setDigits(int state);
    
    void markAllDirty();
    
    std::vector<MidiEvent> getMidiEvents();
private:
    std::mutex m_access;
    std::vector<Light<bool>> pedals;
    std::vector<Light<bool>> miscLights;
    Light<int> digits{0};
};

#endif /* FCBLIGHTS_H */

