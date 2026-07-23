#include "AppBlockerPlugin.h"
#include "VeyonServerInterface.h"
#include "VeyonWorkerInterface.h"
#include "FeatureWorkerManager.h"
#include "AppBlockerDialog.h"
#include <QProcess>
#include <QStringList>
#include <QSettings>

AppBlockerPlugin::AppBlockerPlugin(QObject* parent) :
	QObject(parent),
	m_appBlockerFeature(QStringLiteral("AppBlocker"), Feature::Flag::Action | Feature::Flag::AllComponents,
				  uid(), Feature::Uid(), tr("Uygulama Engelle (Kara Liste)"), {}, tr("Öğrencilerin belirli programları çalıştırmasını engeller."), QStringLiteral(":/core/toast-error.png")),
	m_features({m_appBlockerFeature})
{
	connect(&m_killTimer, &QTimer::timeout, this, &AppBlockerPlugin::killBlockedApps);
}

AppBlockerPlugin::~AppBlockerPlugin()
{
	m_killTimer.stop();
}

bool AppBlockerPlugin::controlFeature(Feature::Uid featureUid, Operation operation, const QVariantMap& arguments,
								const ComputerControlInterfaceList& computerControlInterfaces)
{
	Q_UNUSED(arguments);
	if (featureUid != m_appBlockerFeature.uid()) return false;

	if (operation == Operation::Start) {
		AppBlockerDialog dialog(nullptr);
		if (dialog.exec() == QDialog::Accepted) {
			QStringList apps = dialog.getBlockedApps();
			FeatureMessage msg(featureUid, AppBlockerCommand::UpdateBlacklist);
			msg.addArgument(AppBlockerArgument::AppList, apps.join(","));
			sendFeatureMessage(msg, computerControlInterfaces);
		}
		return true;
	} else if (operation == Operation::Stop) {
		FeatureMessage msg(featureUid, AppBlockerCommand::StopBlocking);
		sendFeatureMessage(msg, computerControlInterfaces);
		return true;
	}
	return false;
}

bool AppBlockerPlugin::handleFeatureMessage(VeyonServerInterface& server,
									  const MessageContext& messageContext,
									  const FeatureMessage& message)
{
	Q_UNUSED(messageContext);
	if (message.featureUid() != m_appBlockerFeature.uid()) return false;

	server.featureWorkerManager().sendMessageToUnmanagedSessionWorker(message);
	return true;
}

bool AppBlockerPlugin::handleFeatureMessage(VeyonWorkerInterface& worker, const FeatureMessage& message)
{
	Q_UNUSED(worker);
	if (message.featureUid() != m_appBlockerFeature.uid()) return false;
	
	auto cmd = message.command<AppBlockerCommand>();
	
	if (cmd == AppBlockerCommand::UpdateBlacklist) {
		QString appString = message.argument(AppBlockerArgument::AppList).toString();
		
		// Save persistently
		QSettings settings("VeyonCommunity", "AppBlocker_Worker");
		settings.setValue("Blacklist", appString.split(",", Qt::SkipEmptyParts));
		
		loadWorkerBlacklist();
		return true;
	} else if (cmd == AppBlockerCommand::StopBlocking) {
		m_killTimer.stop();
		m_blockedApps.clear();
		return true;
	}

	return false;
}

void AppBlockerPlugin::startWorker(VeyonWorkerInterface& worker)
{
	Q_UNUSED(worker);
	loadWorkerBlacklist();
}

void AppBlockerPlugin::loadWorkerBlacklist()
{
	QSettings settings("VeyonCommunity", "AppBlocker_Worker");
	QStringList apps = settings.value("Blacklist").toStringList();
	
	m_blockedApps.clear();
	for(const QString& app : apps) {
		QString trimmed = app.trimmed();
		if (!trimmed.isEmpty()) {
			m_blockedApps.append(trimmed);
		}
	}
	
	if (!m_blockedApps.isEmpty()) {
		m_killTimer.start(5000); // Check every 5 seconds
	} else {
		m_killTimer.stop();
	}
}

void AppBlockerPlugin::killBlockedApps()
{
	for (const QString& app : m_blockedApps) {
#ifdef Q_OS_WIN
		QString target = app;
		if (!target.toLower().endsWith(".exe")) {
			target += ".exe";
		}
		QProcess::execute("taskkill", QStringList() << "/F" << "/IM" << target);
#else
		QProcess::execute("killall", QStringList() << "-9" << app);
#endif
	}
}
