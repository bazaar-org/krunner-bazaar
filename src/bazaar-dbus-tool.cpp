#include "bazaarclient.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QTextStream>
#include <QDebug>
#include <iostream>

using namespace Qt::Literals::StringLiterals;

int main(int argc, char *argv[])
{
    // override Fedora's weird logging rules for Qt apps
    qputenv("QT_FORCE_STDERR_LOGGING", "1");

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("bazaar-dbus-tool"_L1);
    QCoreApplication::setApplicationVersion("1.0"_L1);

    QCommandLineParser parser;
    parser.setApplicationDescription("Test tool for debugging Bazaar D-Bus interaction"_L1);
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption searchOption({"s"_L1, "search"_L1}, "Search for applications"_L1, "query"_L1);
    QCommandLineOption activateOption({"a"_L1, "activate"_L1}, "Activate/launch an application"_L1, "app-id"_L1);
    QCommandLineOption verboseOption({"d"_L1, "verbose"_L1}, "Enable verbose output"_L1);

    parser.addOption(searchOption);
    parser.addOption(activateOption);
    parser.addOption(verboseOption);

    parser.process(app);
    
    if (parser.isSet(verboseOption)) {
        qputenv("QT_LOGGING_RULES", "*.debug=true");
        std::cout << "Verbose mode enabled" << std::endl;
    }
    
    BazaarClient client;

    if (!client.isConnected()) {
        std::cerr << "Error: Could not connect to Bazaar D-Bus service" << std::endl;
        std::cerr << "Make sure Bazaar is running and the search provider is enabled." << std::endl;
        return 1;
    }

    std::cout << "Successfully connected to Bazaar D-Bus service " << client.serviceName()->toStdString() << std::endl;

    if (parser.isSet(searchOption)) {
        QString query = parser.value(searchOption);
        std::cout << "Searching for: " << query.toStdString() << std::endl;

        const auto result = client.search(query);
        const QList<AppSuggestion> &results = result.apps;

        if (results.isEmpty()) {
            std::cout << "No results found for query: " << query.toStdString() << std::endl;
            if (!result.error.isEmpty()) {
                std::cerr << "Error: " << result.error.toStdString() << std::endl;
            }
        } else {
            std::cout << "Found " << results.size() << " results:" << std::endl;
            std::cout << std::endl;

            for (int i = 0; i < results.size(); ++i) {
                const AppSuggestion &app = results[i];
                std::cout << "Result " << (i + 1) << ":" << std::endl;
                std::cout << "  ID: " << app.id.toStdString() << std::endl;
                std::cout << "  Name: " << app.name.toStdString() << std::endl;
                std::cout << "  Description: " << app.description.toStdString() << std::endl;
                std::cout << "  Icon: " << app.iconName.toStdString() << std::endl;
                std::cout << std::endl;
            }
        }
    } else if (parser.isSet(activateOption)) {
        QString appId = parser.value(activateOption);
        std::cout << "Activating application: " << appId.toStdString() << std::endl;

        // Use empty search terms for activation
        QStringList searchTerms;
        bool success = client.activateResult(appId, searchTerms);

        if (success) {
            std::cout << "Successfully activated application: " << appId.toStdString() << std::endl;
        } else {
            std::cerr << "Failed to activate application: " << appId.toStdString() << std::endl;
        }
    } else {
        std::cout << "Usage examples:" << std::endl;
        std::cout << "  " << argv[0] << " --search \"firefox\"" << std::endl;
        std::cout << "  " << argv[0] << " --activate \"org.mozilla.firefox\"" << std::endl;
        std::cout << "  " << argv[0] << " --search \"text editor\" --verbose" << std::endl;
        std::cout << std::endl;
        parser.showHelp();
    }

    return 0;
}
