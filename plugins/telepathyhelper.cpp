#include "telepathyhelper.h"

#include <TelepathyQt/AccountSet>
#include <TelepathyQt/ClientRegistrar>
#include <TelepathyQt/PendingReady>

TelepathyHelper *TelepathyHelper::mTelepathyHelper = 0;

TelepathyHelper::TelepathyHelper(QObject *parent)
    : QObject(parent),
      mChannelHandler(0)
{
    if (!mTelepathyHelper) {
        mTelepathyHelper = this;
    }

    mChatManager = new ChatManager(this);

    mAccountFeatures << Tp::Account::FeatureCore;
    mContactFeatures << Tp::Contact::FeatureAlias
                     << Tp::Contact::FeatureCapabilities;
    mConnectionFeatures << Tp::Connection::FeatureCore
                        << Tp::Connection::FeatureSelfContact;

    mAccountManager = Tp::AccountManager::create(
            Tp::AccountFactory::create(QDBusConnection::sessionBus(), mAccountFeatures),
            Tp::ConnectionFactory::create(QDBusConnection::sessionBus(), mConnectionFeatures),
            Tp::ChannelFactory::create(QDBusConnection::sessionBus()),
            Tp::ContactFactory::create(mContactFeatures));

    connect(mAccountManager->becomeReady(Tp::AccountManager::FeatureCore),
            SIGNAL(finished(Tp::PendingOperation*)),
            SLOT(onAccountManagerReady(Tp::PendingOperation*)));
}

TelepathyHelper::~TelepathyHelper()
{
}

TelepathyHelper *TelepathyHelper::instance()
{
    if (!mTelepathyHelper) {
        mTelepathyHelper = new TelepathyHelper();
    }

    return mTelepathyHelper;
}

ChatManager *TelepathyHelper::chatManager() const
{
    return mChatManager;
}

Tp::AccountPtr TelepathyHelper::account() const
{
    return mAccount;
}

void TelepathyHelper::initializeChannelHandler()
{
    mChannelHandler = new ChannelHandler();
    Tp::AbstractClientPtr handler(mChannelHandler);
    mClientRegistrar->registerClient(handler, "TelephonyApp");

    connect(mChannelHandler, SIGNAL(textChannelAvailable(Tp::TextChannelPtr)),
            mChatManager, SLOT(onTextChannelAvailable(Tp::TextChannelPtr)));

    channelHandlerCreated(mChannelHandler);
}

void TelepathyHelper::registerClients()
{
    Tp::ChannelFactoryPtr channelFactory = Tp::ChannelFactoryPtr::constCast(mAccountManager->channelFactory());
    channelFactory->addCommonFeatures(Tp::Channel::FeatureCore);
    mClientRegistrar = Tp::ClientRegistrar::create(mAccountManager);
    initializeChannelHandler();
}

void TelepathyHelper::onAccountManagerReady(Tp::PendingOperation *op)
{
    Q_UNUSED(op)

    registerClients();

    Tp::AccountSetPtr accountSet = mAccountManager->accountsByProtocol("ufa");

    // for now the application behavior is unexpected if there are more than just one account.
    // the same is valid for the case where there is no accounts
    Q_ASSERT(accountSet->accounts().count() == 1);

    mAccount = accountSet->accounts()[0];
}
