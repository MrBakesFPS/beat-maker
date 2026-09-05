// Mixer commands: inserts, sends, routing. Track index -1 addresses the
// master strip.
#pragma once

#include "Session.h"

namespace beatmaker::model
{

// Put an effect (or nothing) in an insert slot. The instance is created once
// on the message thread so redo reuses it and undo restores the previous
// instance with its state intact.
class SetInsertCommand final : public Command
{
public:
    SetInsertCommand (int trackIndex, int slot, engine::EffectType type, double sampleRate);
    juce::String getName() const override { return type == engine::EffectType::none ? "Remove Insert" : "Add Insert"; }
    void execute (Session&) override;
    void undo (Session&) override;
private:
    int index, slot;
    engine::EffectType type;
    Insert fresh, old;
};

// Put an already-instantiated hosted plugin into a slot (created by the PluginManager on the message thread).
class SetPluginInsertCommand final : public Command
{
public:
    SetPluginInsertCommand (int trackIndex, int slotIndex, std::shared_ptr<engine::Effect> effect, juce::String identifier)
        : index (trackIndex), slot (slotIndex)
    {
        fresh.type = engine::EffectType::plugin;
        fresh.instance = std::move (effect);
        fresh.params = std::make_shared<const engine::InsertParams> (engine::InsertParams { engine::EffectType::plugin, {} });
        fresh.pluginIdentifier = std::move (identifier);
    }
    juce::String getName() const override { return "Insert Plugin"; }
    void execute (Session&) override;
    void undo (Session&) override;
private:
    int index, slot;
    Insert fresh, old;
};

class SetInsertParamsCommand final : public Command
{
public:
    SetInsertParamsCommand (int trackIndex, int slotIndex, std::shared_ptr<const engine::InsertParams> p)
        : index (trackIndex), slot (slotIndex), params (std::move (p)) {}
    juce::String getName() const override { return "Edit Insert"; }
    void execute (Session&) override;
    void undo (Session&) override;
private:
    int index, slot;
    std::shared_ptr<const engine::InsertParams> params, old;
};

// Sidechain key input of an insert (dynamics effects): bus index or -1 for internal.
class SetInsertKeyCommand final : public Command
{
public:
    SetInsertKeyCommand (int trackIndex, int slotIndex, int bus, bool listen) : index (trackIndex), slot (slotIndex), keyBus (bus), keyListen (listen) {}
    juce::String getName() const override { return "Set Key Input"; }
    void execute (Session&) override;
    void undo (Session&) override;
private:
    int index, slot, keyBus;
    bool keyListen;
    int oldBus = -1;
    bool oldListen = false;
};

// Custom impulse response for a Convolution Reverb insert.
class SetInsertImpulseCommand final : public Command
{
public:
    SetInsertImpulseCommand (int trackIndex, int slotIndex, std::shared_ptr<const juce::AudioBuffer<float>> ir, double irSampleRate, juce::String irName, juce::String irPath = {})
        : index (trackIndex), slot (slotIndex), impulse (std::move (ir)), sampleRate (irSampleRate), name (std::move (irName)), path (std::move (irPath)) {}
    juce::String getName() const override { return "Load Impulse Response"; }
    void execute (Session&) override;
    void undo (Session&) override;
private:
    int index, slot;
    std::shared_ptr<const juce::AudioBuffer<float>> impulse, oldImpulse;
    std::shared_ptr<const engine::InsertParams> oldParams;
    double sampleRate, oldSampleRate = 44100.0;
    juce::String name, oldName, path, oldPath;
};

class SetInsertBypassCommand final : public Command
{
public:
    SetInsertBypassCommand (int trackIndex, int slotIndex, bool shouldBypass) : index (trackIndex), slot (slotIndex), bypass (shouldBypass) {}
    juce::String getName() const override { return bypass ? "Bypass Insert" : "Enable Insert"; }
    void execute (Session&) override;
    void undo (Session&) override;
private:
    int index, slot;
    bool bypass, old = false;
};

class SetSendCommand final : public Command
{
public:
    SetSendCommand (int trackIndex, int sendSlot, Send newSend) : index (trackIndex), sendIndex (sendSlot), send (newSend) {}
    juce::String getName() const override { return "Send"; }
    void execute (Session&) override;
    void undo (Session&) override;
private:
    int index, sendIndex;
    Send send, old;
};

class SetTrackRoutingCommand final : public Command
{
public:
    SetTrackRoutingCommand (int trackIndex, int inputBus, int outputBus) : index (trackIndex), in (inputBus), out (outputBus) {}
    juce::String getName() const override { return "Routing"; }
    void execute (Session&) override;
    void undo (Session&) override;
private:
    int index, in, out, oldIn = -1, oldOut = -1;
};

} // namespace beatmaker::model
