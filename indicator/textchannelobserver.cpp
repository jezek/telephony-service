/*
 * Copyright (C) 2012-2013 Canonical, Ltd.
 *
 * Authors:
 *  Gustavo Pichorim Boiko <gustavo.boiko@canonical.com>
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

#include <libnotify/notify.h>
#include "applicationutils.h"
#include "textchannelobserver.h"
#include "messagingmenu.h"
#include "metrics.h"
#include "chatmanager.h"
#include "config.h"
#include "contactutils.h"
#include "ringtone.h"
#include <TelepathyQt/AvatarData>
#include <TelepathyQt/TextChannel>
#include <TelepathyQt/ReceivedMessage>
#include <QContactAvatar>
#include <QContactFetchRequest>
#include <QContactFilter>
#include <QContactPhoneNumber>
#include <QImage>

namespace C {
#include <libintl.h>
}

QTCONTACTS_USE_NAMESPACE

// notification handling

class NotificationData {
public:
    QString phoneNumber;
};

void notification_action(NotifyNotification* notification, char *action, gpointer data)
{
    Q_UNUSED(notification);
    Q_UNUSED(action);

    NotificationData *notificationData = (NotificationData*) data;
    if (notificationData != NULL) {
        // launch the messaging-app to show the message
        ApplicationUtils::openUrl(QString("message:///%1").arg(QString(QUrl::toPercentEncoding(notificationData->phoneNumber))));
    }
    g_object_unref(notification);
}

void delete_notification_data(gpointer data)
{
    if (data != NULL) {
        delete (NotificationData*) data;
    }
}

TextChannelObserver::TextChannelObserver(QObject *parent) :
    QObject(parent)
{
    connect(MessagingMenu::instance(),
            SIGNAL(replyReceived(QString,QString)),
            SLOT(onReplyReceived(QString,QString)));
    connect(MessagingMenu::instance(),
            SIGNAL(messageRead(QString,QString)),
            SLOT(onMessageRead(QString,QString)));
}

void TextChannelObserver::showNotificationForMessage(const Tp::ReceivedMessage &message)
{
    Tp::ContactPtr contact = message.sender();

    // try to match the contact info
    QContactFetchRequest *request = new QContactFetchRequest(this);
    request->setFilter(QContactPhoneNumber::match(contact->id()));

    // add the message to the messaging menu (use hex format to avoid invalid characters)
    QByteArray token(message.messageToken().toUtf8());
    MessagingMenu::instance()->addMessage(contact->id(), token.toHex(), message.received(), message.text());

    // place the notify-notification item only after the contact fetch request is finished, as we can´t simply update
    QObject::connect(request, &QContactAbstractRequest::stateChanged, [request, message, contact]() {
        // only process the results after the finished state is reached
        if (request->state() != QContactAbstractRequest::FinishedState) {
            return;
        }

        QString displayLabel;
        QString avatar;

        if (request->contacts().size() > 0) {
            QContact contact = request->contacts().at(0);
            displayLabel = ContactUtils::formatContactName(contact);
            avatar = contact.detail<QContactAvatar>().imageUrl().toEncoded();
        }

        QString title = QString::fromUtf8(C::gettext("SMS from %1")).arg(displayLabel.isEmpty() ? contact->alias() : displayLabel);

        if (avatar.isEmpty()) {
            avatar = QUrl(telephonyServiceDir() + "assets/avatar-default@18.png").toEncoded();
        }

        qDebug() << title << avatar;
        // show the notification
        NotifyNotification *notification = notify_notification_new(title.toStdString().c_str(),
                                                                   message.text().toStdString().c_str(),
                                                                   avatar.toStdString().c_str());

        // add the callback action
        NotificationData *data = new NotificationData();
        data->phoneNumber = contact->id();
        notify_notification_add_action (notification,
                                        "notification_action",
                                        C::gettext("View message"),
                                        notification_action,
                                        data,
                                        delete_notification_data);

        notify_notification_set_hint_string(notification,
                                            "x-canonical-switch-to-application",
                                            "true");

        GError *error = NULL;
        if (!notify_notification_show(notification, &error)) {
            qWarning() << "Failed to show message notification:" << error->message;
            g_error_free (error);
        }

        Ringtone::instance()->playIncomingMessageSound();
    });

    request->setManager(ContactUtils::sharedManager());
    request->start();
}

Tp::TextChannelPtr TextChannelObserver::channelFromPath(const QString &path)
{
    Q_FOREACH(Tp::TextChannelPtr channel, mChannels) {
        if (channel->objectPath() == path) {
            return channel;
        }
    }

    return Tp::TextChannelPtr(0);
}

void TextChannelObserver::onTextChannelAvailable(Tp::TextChannelPtr textChannel)
{
    connect(textChannel.data(),
            SIGNAL(invalidated(Tp::DBusProxy*,const QString&, const QString&)),
            SLOT(onTextChannelInvalidated()));
    connect(textChannel.data(),
            SIGNAL(messageReceived(const Tp::ReceivedMessage&)),
            SLOT(onMessageReceived(const Tp::ReceivedMessage&)));
    connect(textChannel.data(),
            SIGNAL(pendingMessageRemoved(const Tp::ReceivedMessage&)),
            SLOT(onPendingMessageRemoved(const Tp::ReceivedMessage&)));
    connect(textChannel.data(),
            SIGNAL(messageSent(Tp::Message,Tp::MessageSendingFlags,QString)),
            SLOT(onMessageSent(Tp::Message,Tp::MessageSendingFlags,QString)));

    mChannels.append(textChannel);

    // notify all the messages from the channel
    Q_FOREACH(Tp::ReceivedMessage message, textChannel->messageQueue()) {
        onMessageReceived(message);
    }
}

void TextChannelObserver::onTextChannelInvalidated()
{
    Tp::TextChannelPtr textChannel(qobject_cast<Tp::TextChannel*>(sender()));
    mChannels.removeAll(textChannel);
}

void TextChannelObserver::onMessageReceived(const Tp::ReceivedMessage &message)
{
    // do not place notification items for scrollback messages
    if (!message.isScrollback() && !message.isDeliveryReport() && !message.isRescued()) {
        showNotificationForMessage(message);
        Metrics::instance()->increment(Metrics::ReceivedMessages);
    }
}

void TextChannelObserver::onPendingMessageRemoved(const Tp::ReceivedMessage &message)
{
    MessagingMenu::instance()->removeMessage(message.messageToken());
}

void TextChannelObserver::onReplyReceived(const QString &phoneNumber, const QString &reply)
{
    ChatManager::instance()->sendMessage(QStringList() << phoneNumber, reply);
}

void TextChannelObserver::onMessageRead(const QString &phoneNumber, const QString &encodedMessageId)
{
    QString messageId(QByteArray::fromHex(encodedMessageId.toUtf8()));
    ChatManager::instance()->acknowledgeMessage(phoneNumber, messageId);
}

void TextChannelObserver::onMessageSent(Tp::Message, Tp::MessageSendingFlags, QString)
{
    Metrics::instance()->increment(Metrics::SentMessages);
}