#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <functional>
#include <optional>

struct AppSuggestion {
    QString id;
    QString name;
    QString description;
    QString iconName;
};

struct SearchResult {
    QList<AppSuggestion> apps;
    QString error;
};

class BazaarClient {
public:
    BazaarClient();

    bool isConnected() const;
    std::optional<QString> serviceName() const;

    [[nodiscard]] SearchResult search(const QString &term, const std::function<bool()> &isContextValid = nullptr);
    [[nodiscard]] bool activateResult(const QString &appId, const QStringList &searchTerms);

private:
    std::optional<QString> m_serviceName;

    QStringList getInitialResultSet(const QStringList &terms, QString &error);
    QList<QVariantMap> getResultMetas(const QStringList &resultIds, QString &error);
};
