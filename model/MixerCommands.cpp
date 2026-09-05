#include "MixerCommands.h"

namespace beatmaker::model
{

namespace
{
    Insert* insertAt (Session& s, int index, int slot)
    {
        auto* t = EditAccess::trackOrMaster (s, index);
        return (t != nullptr && juce::isPositiveAndBelow (slot, Track::numInsertSlots)) ? &t->inserts[(size_t) slot] : nullptr;
    }
}

SetInsertCommand::SetInsertCommand (int trackIndex, int slotIndex, engine::EffectType t, double sampleRate)
    : index (trackIndex), slot (slotIndex), type (t)
{
    if (type != engine::EffectType::none)
    {
        fresh.type = type;
        fresh.instance = engine::Effect::create (type, sampleRate);
        fresh.params = std::make_shared<const engine::InsertParams> (engine::Effect::defaultParams (type));
    }
}

void SetInsertCommand::execute (Session& s) { if (auto* i = insertAt (s, index, slot)) { old = *i; *i = fresh; } }
void SetInsertCommand::undo (Session& s)    { if (auto* i = insertAt (s, index, slot)) *i = old; }

void SetPluginInsertCommand::execute (Session& s) { if (auto* i = insertAt (s, index, slot)) { old = *i; *i = fresh; } }
void SetPluginInsertCommand::undo (Session& s)    { if (auto* i = insertAt (s, index, slot)) *i = old; }

void SetInsertParamsCommand::execute (Session& s)
{
    if (auto* i = insertAt (s, index, slot))
    {
        old = i->params; i->params = params;
        if (i->instance != nullptr && params != nullptr) i->instance->paramsChanged (*params);
    }
}
void SetInsertParamsCommand::undo (Session& s)
{
    if (auto* i = insertAt (s, index, slot))
    {
        i->params = old;
        if (i->instance != nullptr && old != nullptr) i->instance->paramsChanged (*old);
    }
}

void SetInsertKeyCommand::execute (Session& s)
{
    if (auto* i = insertAt (s, index, slot)) { oldBus = i->keyBus; oldListen = i->keyListen; i->keyBus = keyBus; i->keyListen = keyListen; }
}
void SetInsertKeyCommand::undo (Session& s) { if (auto* i = insertAt (s, index, slot)) { i->keyBus = oldBus; i->keyListen = oldListen; } }

namespace
{
    engine::ConvolutionEffect* convolutionAt (Session& s, int index, int slot)
    {
        auto* i = insertAt (s, index, slot);
        return i != nullptr && i->type == engine::EffectType::convolution ? dynamic_cast<engine::ConvolutionEffect*> (i->instance.get()) : nullptr;
    }
}

void SetInsertImpulseCommand::execute (Session& s)
{
    if (auto* conv = convolutionAt (s, index, slot))
    {
        oldImpulse = conv->getCustomImpulse(); oldSampleRate = conv->getCustomImpulseRate(); oldName = conv->getCustomImpulseName(); oldPath = conv->getCustomImpulsePath();
        conv->setCustomImpulse (impulse, sampleRate, name, path);
        // Select the custom slot in the stored params so the response is (re)loaded and shown.
        auto* i = insertAt (s, index, slot);
        auto p = std::make_shared<engine::InsertParams> (i->params != nullptr ? *i->params : engine::Effect::defaultParams (engine::EffectType::convolution));
        oldParams = i->params;
        p->values[engine::ConvolutionEffect::impulse] = (float) engine::ConvolutionEffect::custom;
        i->params = p;
        conv->paramsChanged (*p);
    }
}
void SetInsertImpulseCommand::undo (Session& s)
{
    if (auto* conv = convolutionAt (s, index, slot))
    {
        conv->setCustomImpulse (oldImpulse, oldSampleRate, oldName, oldPath);
        auto* i = insertAt (s, index, slot);
        i->params = oldParams;
        if (i->params != nullptr) conv->paramsChanged (*i->params);
    }
}

void SetInsertBypassCommand::execute (Session& s) { if (auto* i = insertAt (s, index, slot)) { old = i->bypass; i->bypass = bypass; } }
void SetInsertBypassCommand::undo (Session& s)    { if (auto* i = insertAt (s, index, slot)) i->bypass = old; }

void SetSendCommand::execute (Session& s)
{
    if (auto* t = EditAccess::trackOrMaster (s, index); t != nullptr && juce::isPositiveAndBelow (sendIndex, Track::numSendSlots))
    {
        old = t->sends[(size_t) sendIndex];
        send.bus = send.bus >= Track::numBuses ? -1 : send.bus;
        send.gain = juce::jlimit (0.0f, 2.0f, send.gain);
        t->sends[(size_t) sendIndex] = send;
    }
}
void SetSendCommand::undo (Session& s)
{
    if (auto* t = EditAccess::trackOrMaster (s, index); t != nullptr && juce::isPositiveAndBelow (sendIndex, Track::numSendSlots))
        t->sends[(size_t) sendIndex] = old;
}

void SetTrackRoutingCommand::execute (Session& s)
{
    if (auto* t = EditAccess::trackOrMaster (s, index))
    {
        oldIn = t->inputBus; oldOut = t->outputBus;
        t->inputBus = in >= Track::numBuses ? -1 : in;
        t->outputBus = out >= Track::numBuses ? -1 : out;
    }
}
void SetTrackRoutingCommand::undo (Session& s)
{
    if (auto* t = EditAccess::trackOrMaster (s, index)) { t->inputBus = oldIn; t->outputBus = oldOut; }
}

} // namespace beatmaker::model
