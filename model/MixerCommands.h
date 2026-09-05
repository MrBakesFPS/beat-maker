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
