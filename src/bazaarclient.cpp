#include "bazaarclient.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusArgument>
#include <QDBusVariant>
#include <QDebug>
#include <QDateTime>

using namespace Qt::Literals::StringLiterals;

// TODO: make this configurable in a KCM
static constexpr int kMaxNumResults = 6;

static const QStringList kDBusServiceNames = {
    "io.github.kolunmi.Bazaar.SearchProvider"_L1,
    "io.github.kolunmi.Bazaar"_L1,
};

static const QString kDBusServicePath = "/io/github/kolunmi/Bazaar/SearchProvider"_L1;
static const QString kDBusServiceInterface = "org.gnome.Shell.SearchProvider2"_L1;
static const QString kNoProviderError = "No Bazaar search provider found on the session bus"_L1;

struct ResultMetas {
    QList<QVariantMap> metas;
};

Q_DECLARE_METATYPE(ResultMetas)

const QDBusArgument& operator>>(const QDBusArgument& arg, ResultMetas& metas)
{
    // Start parsing aa{sv}
    arg.beginArray();

    int elementIndex = 0;
    while (!arg.atEnd()) {
        qDebug() << "Processing element" << elementIndex++ << "with type:" << arg.currentType();

        QVariantMap meta;
        arg.beginMap();
        
        while (!arg.atEnd()) {
            if (arg.currentType() != QDBusArgument::MapEntryType) {
                qWarning() << "  Expected MapEntryType but got:" << arg.currentType();
                break;
            }

            arg.beginMapEntry();

            QString key;
            arg >> key;
            
            QDBusVariant dbusVariant;
            arg >> dbusVariant;
            QVariant value = dbusVariant.variant();
            
            arg.endMapEntry();
            
            meta[key] = value;
        }
        arg.endMap();
        
        metas.metas.append(meta);
        qDebug() << "  Added metadata map with" << meta.size() << "entries";
    }

    arg.endArray();
    return arg;
}

namespace {

// Pick the first candidate bus name that is registered or activatable
std::optional<QString> resolveServiceName()
{
    QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface();
    if (!bus) {
        qWarning() << "BazaarClient: no session bus available";
        return std::nullopt;
    }

    const QStringList registered = bus->registeredServiceNames().value();
    const QStringList activatable = bus->activatableServiceNames().value();

    for (const QString &candidate : kDBusServiceNames) {
        if (registered.contains(candidate) || activatable.contains(candidate)) {
            return candidate;
        }
    }

    return std::nullopt;
}

QDBusMessage callProvider(const QString &serviceName, const QString &method, const QVariantList &args)
{
    QDBusMessage call = QDBusMessage::createMethodCall(serviceName, kDBusServicePath, kDBusServiceInterface, method);
    call.setArguments(args);
    return QDBusConnection::sessionBus().call(call);
}

} // namespace

BazaarClient::BazaarClient() {
    m_serviceName = resolveServiceName();

    if (!m_serviceName) {
        qWarning() << "BazaarClient: " << kNoProviderError;
        qWarning() << "BazaarClient: Make sure Bazaar is installed and the search provider is enabled.";
    } else {
        qDebug() << "BazaarClient: using Bazaar D-Bus service" << *m_serviceName;
    }
}

bool BazaarClient::isConnected() const {
    return m_serviceName.has_value();
}

std::optional<QString> BazaarClient::serviceName() const {
    return m_serviceName;
}

SearchResult BazaarClient::search(const QString &term, const std::function<bool()> &isContextValid) {
    SearchResult result;

    if (!m_serviceName) {
        result.error = kNoProviderError;
        qDebug() << "BazaarClient::search:" << result.error;
        return result;
    }

    if (term.length() < 2) {
        result.error = "Search term too short (minimum 2 characters)"_L1;
        return result;
    }

    // Split search term into individual terms
    QStringList terms = term.split(QLatin1Char(' '), Qt::SkipEmptyParts);

    if (isContextValid && !isContextValid()) {
        return result;
    }

    // Get initial result set
    QStringList resultIds = getInitialResultSet(terms, result.error);
    if (resultIds.isEmpty()) {
        qDebug() << "BazaarClient::search: No results returned from Bazaar for query:" << term;
        return result;
    }

    if (isContextValid && !isContextValid()) {
        return result;
    }

    qDebug() << "BazaarClient::search: Bazaar returned" << resultIds.size() << "result IDs (will be truncated to " << kMaxNumResults << ")";

    if (resultIds.size() > kMaxNumResults) {
        resultIds = resultIds.mid(0, kMaxNumResults);
    }

    QList<QVariantMap> metas = getResultMetas(resultIds, result.error);

    // Extract metadata for each result
    for (int i = 0; i < resultIds.size() && i < metas.size(); ++i) {
        if (isContextValid && !isContextValid()) {
            break;
        }

        const QVariantMap &meta = metas[i];

        AppSuggestion suggestion;
        suggestion.id = resultIds[i];
        suggestion.name = meta.value(QStringLiteral("name")).toString();
        suggestion.description = meta.value(QStringLiteral("description")).toString();
        
        suggestion.iconName = meta.value(QStringLiteral("gicon")).toString();
        if (suggestion.iconName.isEmpty()) {
            // Handle serialized icon variant
            QVariant iconVariant = meta.value(QStringLiteral("icon"));
            if (iconVariant.isValid()) {
                // For now, use a fallback icon since handling serialized GIcon is complex
                suggestion.iconName = QStringLiteral("application-x-flatpak");
            }
        }

        // If no icon is provided, use a default Flatpak icon
        if (suggestion.iconName.isEmpty()) {
            suggestion.iconName = QStringLiteral("application-x-flatpak");
        }

        // Skip if we couldn't get a name
        if (suggestion.name.isEmpty()) {
            qWarning() << "BazaarClient::search: Skipping result with empty name:" << suggestion.id;
            continue;
        }

        result.apps.append(suggestion);
    }

    return result;
}

bool BazaarClient::activateResult(const QString &appId, const QStringList &searchTerms) {
    if (!m_serviceName) {
        qWarning() << "BazaarClient::activateResult:" << kNoProviderError;
        return false;
    }

    qDebug() << "BazaarClient::activateResult: Activating app ID:" << appId;

    uint timestamp = static_cast<uint>(QDateTime::currentSecsSinceEpoch());

    QDBusMessage reply = callProvider(*m_serviceName, "ActivateResult"_L1, {appId, searchTerms, timestamp});

    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "BazaarClient::activateResult: Failed to activate result:" << reply.errorMessage();
        return false;
    }

    return true;
}

QStringList BazaarClient::getInitialResultSet(const QStringList &terms, QString &error) {
    QDBusMessage reply = callProvider(*m_serviceName, "GetInitialResultSet"_L1, {terms});

    if (reply.type() == QDBusMessage::ErrorMessage) {
        error = reply.errorMessage();
        qWarning() << "BazaarClient::getInitialResultSet: Failed to get search results:" << error;
        return QStringList();
    }

    if (reply.arguments().isEmpty()) {
        error = "No arguments in GetInitialResultSet reply"_L1;
        qWarning() << "BazaarClient::getInitialResultSet:" << error;
        return QStringList();
    }

    return reply.arguments().at(0).toStringList();
}

QList<QVariantMap> BazaarClient::getResultMetas(const QStringList &resultIds, QString &error) {
    ResultMetas metas;
        
    QDBusMessage metaReply = callProvider(*m_serviceName, "GetResultMetas"_L1, {resultIds});

    if (metaReply.type() == QDBusMessage::ErrorMessage) {
        error = metaReply.errorMessage();
        qWarning() << "BazaarClient::getResultMetas: Failed to get result metadata:" << error;
        return {};
    }


    if (metaReply.arguments().isEmpty()) {
        error = "No arguments in GetResultMetas reply"_L1;
        qWarning() << "BazaarClient::getResultMetas:" << error;
        return {};
    }

    // Get the first argument which should be aa{sv}
    QVariant metaVariant = metaReply.arguments().at(0);
    qDebug() << "BazaarClient::getResultMetas: Metadata variant type:" << metaVariant.typeName();

    QDBusArgument metaArg = metaVariant.value<QDBusArgument>();
    metaArg >> metas;

    return metas.metas;
}
