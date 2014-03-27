/*
 * Copyright (C) 2012 Canonical, Ltd.
 *
 * Authors:
 *  Gustavo Pichorim Boiko <gustavo.boiko@canonical.com>
 *  Tiago Salem Herrmann <tiago.herrmann@canonical.com>
 *
 * This file is part of telephony-service.
 *
 * telephony-service is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3.
 *
 * telephony-service is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "callentry.h"
#include "telepathyhelper.h"

#include <QDBusReply>
#include <QTime>
#include <TelepathyQt/Contact>
#include <TelepathyQt/PendingReady>
#include <TelepathyQt/Connection>

#define TELEPATHY_MUTE_IFACE "org.freedesktop.Telepathy.Call1.Interface.Mute"
#define TELEPATHY_CALL_IFACE "org.freedesktop.Telepathy.Channel.Type.Call1"
#define DBUS_PROPERTIES_IFACE "org.freedesktop.DBus.Properties"
#define PROPERTY_SPEAKERMODE "SpeakerMode"

CallEntry::CallEntry(const Tp::CallChannelPtr &channel, QObject *parent) :
    QObject(parent),
    mChannel(channel),
    mVoicemail(false),
    mLocalMuteState(false),
    mMuteInterface(channel->busName(), channel->objectPath(), TELEPATHY_MUTE_IFACE),
    mSpeakerInterface(channel->busName(), channel->objectPath(), CANONICAL_TELEPHONY_SPEAKER_IFACE),
    mHasSpeakerProperty(false),
    mSpeakerMode(false)
{
    mAccount = TelepathyHelper::instance()->accountForConnection(mChannel->connection());
    setupCallChannel();

    // connect to the DBus signal
    connect(TelepathyHelper::instance()->handlerInterface(),
            SIGNAL(CallPropertiesChanged(QString, QVariantMap)),
            SLOT(onCallPropertiesChanged(QString,QVariantMap)));

    Q_EMIT incomingChanged();
}

void CallEntry::onSpeakerChanged(bool active)
{
    mSpeakerMode = active;
    Q_EMIT speakerChanged();
}

void CallEntry::onCallPropertiesChanged(const QString &objectPath, const QVariantMap &properties)
{
    if (objectPath != mChannel->objectPath()) {
        return;
    }
    updateChannelProperties(properties);
}

void CallEntry::setupCallChannel()
{
    connect(mChannel.data(),
            SIGNAL(callStateChanged(Tp::CallState)),
            SLOT(onCallStateChanged(Tp::CallState)));
    connect(mChannel.data(),
            SIGNAL(callFlagsChanged(Tp::CallFlags)),
            SLOT(onCallFlagsChanged(Tp::CallFlags)));
    connect(mChannel.data(),
            SIGNAL(localHoldStateChanged(Tp::LocalHoldState,Tp::LocalHoldStateReason)),
            SIGNAL(heldChanged()));

    connect(&mMuteInterface,
            SIGNAL(MuteStateChanged(uint)),
            SLOT(onMutedChanged(uint)));

    refreshProperties();

    mHasSpeakerProperty = mProperties.contains(PROPERTY_SPEAKERMODE);
    if (mHasSpeakerProperty) {
        connect(&mSpeakerInterface, SIGNAL(SpeakerChanged(bool)), SLOT(onSpeakerChanged(bool)));
    }

    onCallStateChanged(mChannel->callState());

    Q_EMIT heldChanged();
    Q_EMIT phoneNumberChanged();
    Q_EMIT dialingChanged();
}

void CallEntry::updateChannelProperties(const QVariantMap &properties)
{
    QVariantMap props = properties;
    // fetch the properties if the map is empty
    if (props.isEmpty()) {
        QDBusInterface *phoneAppHandler = TelepathyHelper::instance()->handlerInterface();
        QDBusReply<QVariantMap> reply = phoneAppHandler->call("GetCallProperties", mChannel->objectPath());
        if (!reply.isValid()) {
            return;
        }

        props = reply.value();
    }

    QDateTime timestamp;
    if (props.contains("timestamp")) {
        props["timestamp"].value<QDBusArgument>() >> timestamp;
    }
    if (props.contains("activeTimestamp")) {
        props["activeTimestamp"].value<QDBusArgument>() >> mActiveTimestamp;
    }

    mChannel->setProperty("dtmfString", props["dtmfString"]);
    mChannel->setProperty("timestamp", timestamp);
    mChannel->setProperty("activeTimestamp", mActiveTimestamp);

    Q_EMIT dtmfStringChanged();
}

void CallEntry::timerEvent(QTimerEvent *event)
{
    Q_EMIT elapsedTimeChanged();
}

void CallEntry::refreshProperties()
{
     QDBusInterface callChannelIface(mChannel->busName(), mChannel->objectPath(), DBUS_PROPERTIES_IFACE);

     QDBusMessage reply = callChannelIface.call("GetAll", TELEPATHY_CALL_IFACE);
     QVariantList args = reply.arguments();
     QMap<QString, QVariant> map = qdbus_cast<QMap<QString, QVariant> >(args[0]);

     reply = callChannelIface.call("GetAll", CANONICAL_TELEPHONY_SPEAKER_IFACE);
     args = reply.arguments();
     QMap<QString, QVariant> map2 = qdbus_cast<QMap<QString, QVariant> >(args[0]);

     mProperties.clear();
     QMapIterator<QString, QVariant> i(map);
     while(i.hasNext()) {
         i.next();
         mProperties[i.key()] = i.value();
     }
     QMapIterator<QString, QVariant> i2(map2);
     while(i2.hasNext()) {
         i2.next();
         mProperties[i2.key()] = i2.value();
     }

     onSpeakerChanged(mProperties[PROPERTY_SPEAKERMODE].toBool());
}

bool CallEntry::dialing() const
{
    return !incoming() && (mChannel->callState() == Tp::CallStateInitialised);
}

bool CallEntry::incoming() const
{
    if (!mAccount) {
        return false;
    }

    return mChannel->initiatorContact() != mAccount->connection()->selfContact();
}

bool CallEntry::ringing() const
{
    return incoming() && (mChannel->callState() == Tp::CallStateInitialised);
}

QString CallEntry::phoneNumber() const
{
    if (!mChannel->actualFeatures().contains(Tp::CallChannel::FeatureCore)) {
        return "";
    }
    return mChannel->targetContact()->id();
}

QString CallEntry::dtmfString() const
{
    return mChannel->property("dtmfString").toString();
}

void CallEntry::sendDTMF(const QString &key)
{
    QDBusInterface *phoneAppHandler = TelepathyHelper::instance()->handlerInterface();
    phoneAppHandler->call("SendDTMF", mChannel->objectPath(), key);
}

void CallEntry::endCall()
{
    QDBusInterface *phoneAppHandler = TelepathyHelper::instance()->handlerInterface();
    phoneAppHandler->call("HangUpCall", mChannel->objectPath());
}

Tp::CallChannelPtr CallEntry::channel() const
{
    return mChannel;
}

bool CallEntry::isHeld() const
{
    if (!mChannel->actualFeatures().contains(Tp::CallChannel::FeatureLocalHoldState)) {
        return false;
    }
    return (mChannel->localHoldState() == Tp::LocalHoldStateHeld);
}

void CallEntry::setHold(bool hold)
{
    QDBusInterface *phoneAppHandler = TelepathyHelper::instance()->handlerInterface();
    phoneAppHandler->call("SetHold", mChannel->objectPath(), hold);
}

void CallEntry::onMutedChanged(uint state)
{
    // Replace this by a Mute interface method call when it
    // becomes available in telepathy-qt
    mLocalMuteState = (state == 1);
    Q_EMIT mutedChanged();
}

bool CallEntry::isMuted() const
{
    // Replace this by a Mute interface method call when it
    // becomes available in telepathy-qt
    return mLocalMuteState;
}

void CallEntry::setMute(bool value)
{
    QDBusInterface *phoneAppHandler = TelepathyHelper::instance()->handlerInterface();
    phoneAppHandler->call("SetMuted", mChannel->objectPath(), value);
}

void CallEntry::onCallStateChanged(Tp::CallState state)
{
    // fetch the channel properties from the handler
    updateChannelProperties();

    switch (state) {
    case Tp::CallStateEnded:
        Q_EMIT callEnded();
        break;
    case Tp::CallStateActive:
        startTimer(1000);
        Q_EMIT callActive();
        Q_EMIT activeChanged();
        break;
    }

    Q_EMIT dialingChanged();
}

void CallEntry::onCallFlagsChanged(Tp::CallFlags flags)
{
    Q_UNUSED(flags)

    Q_EMIT ringingChanged();
}

void CallEntry::setVoicemail(bool voicemail)
{
    mVoicemail = voicemail;
    Q_EMIT voicemailChanged();
}

bool CallEntry::isVoicemail() const
{
    return mVoicemail;
}

int CallEntry::elapsedTime() const
{
    if (!mActiveTimestamp.isValid()) {
        return 0;
    }
    return mActiveTimestamp.secsTo(QDateTime::currentDateTimeUtc());
}

bool CallEntry::isActive() const
{
    return (mChannel->callState() == Tp::CallStateActive);
}

bool CallEntry::isSpeakerOn()
{
    if (mHasSpeakerProperty) {
        return mSpeakerMode;
    }

    return false;
}

void CallEntry::setSpeaker(bool speaker)
{
    if (!mHasSpeakerProperty) {
        return;
    }

    QDBusInterface *phoneAppHandler = TelepathyHelper::instance()->handlerInterface();
    phoneAppHandler->call("SetSpeakerMode", mChannel->objectPath(), speaker);
}

