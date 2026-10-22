#pragma once

struct NeuronVisualState
{
    float activity;
    bool spike;
    bool ablated;
    bool selected;
    bool differenceMode;
    float deltaActivity;

    NeuronVisualState()
        : activity(0.15f), spike(false), ablated(false), selected(false),
          differenceMode(false), deltaActivity(0.0f)
    {
    }
};
