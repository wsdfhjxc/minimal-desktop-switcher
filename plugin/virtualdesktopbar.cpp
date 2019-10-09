#include "virtualdesktopbar.h"

#include "kwindbusdesktopshim.h"

#include <KWindowSystem>
#include <KGlobalAccel>
#include <QThread>
#include <QTimer>

VirtualDesktopBar::VirtualDesktopBar(QObject* parent) : QObject(parent),
                                     netRootInfo(QX11Info::connection(), 0),
                                     dbusInterface("org.kde.kglobalaccel", "/component/kwin", "") {

    cfg_keepOneEmptyDesktop = false;
    cfg_dropRedundantDesktops = false;

    setUpSignalForwarding();
    setUpGlobalKeyboardShortcuts();

    currentDesktopNumber = KWindowSystem::currentDesktop();
    recentDesktopNumber = currentDesktopNumber;
}

const QList<QString> VirtualDesktopBar::getDesktopNames() const {
    QList<QString> desktopNames;
    const int numberOfDesktops = KWindowSystem::numberOfDesktops();
    for (int desktopNumber = 0; desktopNumber < numberOfDesktops; desktopNumber++) {
        const QString& desktopName = KWindowSystem::desktopName(desktopNumber + 1);
        desktopNames << desktopName;
    }
    return desktopNames;
}

const QString VirtualDesktopBar::getCurrentDesktopName() const {
    return KWindowSystem::desktopName(currentDesktopNumber);
}

int VirtualDesktopBar::getCurrentDesktopNumber() const {
    return currentDesktopNumber;
}

void VirtualDesktopBar::switchToDesktop(const int desktopNumber) {
    if (desktopNumber < 1 || desktopNumber > KWindowSystem::numberOfDesktops()) {
        return;
    }
    KWindowSystem::setCurrentDesktop(desktopNumber);
}

void VirtualDesktopBar::switchToRecentDesktop() {
    switchToDesktop(recentDesktopNumber);
}

void VirtualDesktopBar::addNewDesktop(bool guard, const QString desktopName) {
    if (guard && cfg_keepOneEmptyDesktop && cfg_dropRedundantDesktops) {
        return;
    }
    const int numberOfDesktops = KWindowSystem::numberOfDesktops();
    netRootInfo.setNumberOfDesktops(numberOfDesktops + 1);
    if (!desktopName.isNull() && !desktopName.isEmpty()) {
        renameDesktop(numberOfDesktops + 1, desktopName);
    }
    if (guard && !cfg_dropRedundantDesktops && !cfg_newDesktopCommand.isEmpty()) {
        QTimer::singleShot(100, [=] {
            QString command = "(" + cfg_newDesktopCommand + ")&";
            system(command.toStdString().c_str());
        });
    }
}

bool VirtualDesktopBar::canRemoveDesktop(const int desktopNumber) {
    if (KWindowSystem::numberOfDesktops() == 1) {
        return false;
    }
    if (cfg_keepOneEmptyDesktop) {
        bool desktopEmpty = false;
        const QList<int> emptyDesktops = getEmptyDesktops();
        for (int emptyDesktopNumber : emptyDesktops) {
            if (emptyDesktopNumber == desktopNumber) {
                desktopEmpty = true;
                break;
            }
        }
        if (desktopEmpty && emptyDesktops.length() == 1) {
            return false;
        }
    }
    return true;
}

void VirtualDesktopBar::removeDesktop(const int desktopNumber) {
    const int numberOfDesktops = KWindowSystem::numberOfDesktops();
    if (numberOfDesktops == 1) {
        return;
    }

    emit desktopRemoveRequested(desktopNumber);

    if (desktopNumber > 0 && desktopNumber != numberOfDesktops) {
        const QList<WId> windowsAfterDesktop = getWindows(desktopNumber, true);
        for (WId id : windowsAfterDesktop) {
            const KWindowInfo info = KWindowInfo(id, NET::WMDesktop);
            windowDesktopChangesToIgnore << QPair<WId, int>(id, info.desktop() - 1);
            KWindowSystem::setOnDesktop(id, info.desktop() - 1);
        }

        QList<QString> desktopNames = getDesktopNames();
        for (int i = desktopNumber - 1; i < numberOfDesktops - 1; i++) {
            const QString desktopName = desktopNames[i + 1];
            renameDesktop(i + 1, desktopName);
        }
    }

    if (desktopNumber == numberOfDesktops && currentDesktopNumber == desktopNumber) {
        currentDesktopNumber = recentDesktopNumber;
    }

    if (recentDesktopNumber == desktopNumber) {
        recentDesktopNumber = 0;
    } else if (recentDesktopNumber > desktopNumber) {
        recentDesktopNumber -= 1;
    }

    netRootInfo.setNumberOfDesktops(numberOfDesktops - 1);
}

void VirtualDesktopBar::removeCurrentDesktop() {
    if (currentDesktopNumber == KWindowSystem::numberOfDesktops()) {
        removeLastDesktop();
        return;
    }
    if (canRemoveDesktop(currentDesktopNumber)) {
        dbusInterface.call("invokeShortcut", "VDB-Event-RemoveCurrentDesktop-Before");
        QThread::msleep(100);
        removeDesktop(currentDesktopNumber);
        dbusInterface.call("invokeShortcut", "VDB-Event-RemoveCurrentDesktop-After");
    }
}

void VirtualDesktopBar::removeLastDesktop() {
    if (canRemoveDesktop(KWindowSystem::numberOfDesktops())) {
        dbusInterface.call("invokeShortcut", "VDB-Event-RemoveLastDesktop-Before");
        QThread::msleep(100);
        removeDesktop(KWindowSystem::numberOfDesktops());
        dbusInterface.call("invokeShortcut", "VDB-Event-RemoveLastDesktop-After");
    }
}

void VirtualDesktopBar::renameDesktop(const int desktopNumber, const QString desktopName) {
    KWindowSystem::setDesktopName(desktopNumber, desktopName);

    // See issue #6.
    renameDesktopDBus(desktopNumber, desktopName);
}

void VirtualDesktopBar::renameDesktopDBus(const int desktopNumber, const QString desktopName) {
    QDBusInterface interface("org.kde.KWin", "/VirtualDesktopManager", "");
    QDBusMessage reply = interface.call("Get", "org.kde.KWin.VirtualDesktopManager", "desktops");
    if (reply.type() != QDBusMessage::ReplyMessage) {
        return;
    }

    QDBusVariant var = reply.arguments().at(0).value<QDBusVariant>();
    QDBusArgument arg = var.variant().value<QDBusArgument>();
    if (arg.currentType() != QDBusArgument::ArrayType) {
        return;
    }

    QList<KWinDBusDesktopShim> list;
    arg >> list;
    for (const KWinDBusDesktopShim& shim : list) {
        if (shim.number + 1 == desktopNumber) {
            interface.call("setDesktopName", shim.id, desktopName);
            break;
        }
    }
}

void VirtualDesktopBar::renameCurrentDesktop(const QString desktopName) {
    renameDesktop(currentDesktopNumber, desktopName);
}

void VirtualDesktopBar::swapDesktop(const int desktopNumber, const int targetDesktopNumber) {
    if (targetDesktopNumber == desktopNumber) {
        return;
    }

    QList<WId> windowsFromDesktop = getWindows(desktopNumber);
    QList<WId> windowsFromTargetDesktop = getWindows(targetDesktopNumber);

    for (WId id : windowsFromDesktop) {
        windowDesktopChangesToIgnore << QPair<WId, int>(id, targetDesktopNumber);
        KWindowSystem::setOnDesktop(id, targetDesktopNumber);
    }

    for (WId id : windowsFromTargetDesktop) {
        windowDesktopChangesToIgnore << QPair<WId, int>(id, desktopNumber);
        KWindowSystem::setOnDesktop(id, desktopNumber);
    }

    const QString desktopName = KWindowSystem::desktopName(desktopNumber);
    const QString targetDesktopName = KWindowSystem::desktopName(targetDesktopNumber);

    renameDesktop(desktopNumber, targetDesktopName);
    renameDesktop(targetDesktopNumber, desktopName);

    if (currentDesktopNumber == desktopNumber) {
        currentDesktopNumber = targetDesktopNumber;
    } else if (currentDesktopNumber == targetDesktopNumber) {
        currentDesktopNumber = desktopNumber;
    }

    if (recentDesktopNumber == desktopNumber) {
        recentDesktopNumber = targetDesktopNumber;
    } else if (recentDesktopNumber == targetDesktopNumber) {
        recentDesktopNumber = desktopNumber;
    }
}

void VirtualDesktopBar::moveDesktop(const int desktopNumber, const int moveStep) {
    int targetDesktopNumber = desktopNumber + moveStep;
    if (targetDesktopNumber < 1 || targetDesktopNumber > KWindowSystem::numberOfDesktops()) {
        return;
    }

    const int modifier = targetDesktopNumber > desktopNumber ? 1 : -1;
    for (int i = desktopNumber; i != targetDesktopNumber; i += modifier) {
        swapDesktop(i, i + modifier);
    }
}

void VirtualDesktopBar::moveDesktopToLeft(const int desktopNumber) {
    moveDesktop(desktopNumber, -1);
}

void VirtualDesktopBar::moveDesktopToRight(const int desktopNumber) {
    moveDesktop(desktopNumber, 1);
}

void VirtualDesktopBar::moveCurrentDesktopToLeft() {
    if (currentDesktopNumber == 1) {
        return;
    }

    dbusInterface.call("invokeShortcut", "VDB-Event-MoveCurrentDesktopToLeft-Before");
    QThread::msleep(100);
    moveDesktopToLeft(currentDesktopNumber);
    switchToDesktop(currentDesktopNumber);
    dbusInterface.call("invokeShortcut", "VDB-Event-MoveCurrentDesktopToLeft-After");
}

void VirtualDesktopBar::moveCurrentDesktopToRight() {
    if (currentDesktopNumber == KWindowSystem::numberOfDesktops()) {
        return;
    }

    dbusInterface.call("invokeShortcut", "VDB-Event-MoveCurrentDesktopToRight-Before");
    QThread::msleep(100);
    moveDesktopToRight(currentDesktopNumber);
    switchToDesktop(currentDesktopNumber);
    dbusInterface.call("invokeShortcut", "VDB-Event-MoveCurrentDesktopToRight-After");
}

void VirtualDesktopBar::onCurrentDesktopChanged(const int desktopNumber) {
    if (desktopNumber != currentDesktopNumber) {
        recentDesktopNumber = currentDesktopNumber;
    }
    currentDesktopNumber = desktopNumber;
    emit currentDesktopChanged(desktopNumber);
}

void VirtualDesktopBar::onDesktopAmountChanged(const int desktopAmount) {
    if (cfg_keepOneEmptyDesktop) {
        int numberOfEmptyDesktops = getEmptyDesktops().length();
        if (numberOfEmptyDesktops == 0) {
            addNewDesktop(false);
            return;
        } else if (numberOfEmptyDesktops > 1 && cfg_dropRedundantDesktops) {
            removeEmptyDesktops();
            return;
        }
    }
    const QList<int> emptyDesktops = getEmptyDesktops();
    if (!cfg_emptyDesktopName.isEmpty()) {
        renameEmptyDesktops(emptyDesktops);
    }
    emit emptyDesktopsUpdated(emptyDesktops);
    emit desktopAmountChanged(desktopAmount);
}

void VirtualDesktopBar::setUpSignalForwarding() {

    QObject::connect(KWindowSystem::self(), &KWindowSystem::currentDesktopChanged,
                     this, &VirtualDesktopBar::onCurrentDesktopChanged);

    QObject::connect(KWindowSystem::self(), &KWindowSystem::numberOfDesktopsChanged,
                     this, &VirtualDesktopBar::onDesktopAmountChanged);

    QObject::connect(KWindowSystem::self(), &KWindowSystem::desktopNamesChanged,
                     this, &VirtualDesktopBar::desktopNamesChanged);

    QObject::connect(KWindowSystem::self(), &KWindowSystem::windowAdded,
                     this, &VirtualDesktopBar::onWindowAdded);

    QObject::connect(KWindowSystem::self(),
                     static_cast<void (KWindowSystem::*)(WId, NET::Properties, NET::Properties2)>
                     (&KWindowSystem::windowChanged),
                     this, &VirtualDesktopBar::onWindowChanged);
    
    QObject::connect(KWindowSystem::self(), &KWindowSystem::windowRemoved,
                     this, &VirtualDesktopBar::onWindowRemoved);
}

void VirtualDesktopBar::setUpGlobalKeyboardShortcuts() {
    actionCollection = new KActionCollection(this, QStringLiteral("kwin"));

    actionSwitchToRecentDesktop = actionCollection->addAction(QStringLiteral("switchToRecentDesktop"));
    actionSwitchToRecentDesktop->setText("Switch to Recent Desktop");
    QObject::connect(actionSwitchToRecentDesktop, &QAction::triggered, this, [this]() {
        switchToRecentDesktop();
    });
    KGlobalAccel::setGlobalShortcut(actionSwitchToRecentDesktop, QKeySequence());

    actionAddNewDesktop = actionCollection->addAction(QStringLiteral("addNewDesktop"));
    actionAddNewDesktop->setText("Add New Desktop");
    actionAddNewDesktop->setIcon(QIcon::fromTheme(QStringLiteral("list-add")));
    QObject::connect(actionAddNewDesktop, &QAction::triggered, this, [this]() {
        addNewDesktop();
    });
    KGlobalAccel::setGlobalShortcut(actionAddNewDesktop, QKeySequence());

    actionRemoveLastDesktop = actionCollection->addAction(QStringLiteral("removeLastDesktop"));
    actionRemoveLastDesktop->setText("Remove Last Desktop");
    actionRemoveLastDesktop->setIcon(QIcon::fromTheme(QStringLiteral("list-remove")));
    QObject::connect(actionRemoveLastDesktop, &QAction::triggered, this, [this]() {
        removeLastDesktop();
    });
    KGlobalAccel::setGlobalShortcut(actionRemoveLastDesktop, QKeySequence());

    actionRemoveCurrentDesktop = actionCollection->addAction(QStringLiteral("removeCurrentDesktop"));
    actionRemoveCurrentDesktop->setText("Remove Current Desktop");
    actionRemoveCurrentDesktop->setIcon(QIcon::fromTheme(QStringLiteral("list-remove")));
    QObject::connect(actionRemoveCurrentDesktop, &QAction::triggered, this, [this]() {
        removeCurrentDesktop();
    });
    KGlobalAccel::setGlobalShortcut(actionRemoveCurrentDesktop, QKeySequence());

    actionRenameCurrentDesktop = actionCollection->addAction(QStringLiteral("renameCurrentDesktop"));
    actionRenameCurrentDesktop->setText("Rename Current Desktop");
    actionRenameCurrentDesktop->setIcon(QIcon::fromTheme(QStringLiteral("edit-rename")));
    QObject::connect(actionRenameCurrentDesktop, &QAction::triggered, this, [this]() {
        emit currentDesktopNameChangeRequested();
    });
    KGlobalAccel::setGlobalShortcut(actionRenameCurrentDesktop, QKeySequence());

    actionMoveCurrentDesktopToLeft = actionCollection->addAction(QStringLiteral("moveCurrentDesktopToLeft"));
    actionMoveCurrentDesktopToLeft->setText("Move Current Desktop to Left");
    actionMoveCurrentDesktopToLeft->setIcon(QIcon::fromTheme(QStringLiteral("edit-rename")));
    QObject::connect(actionMoveCurrentDesktopToLeft, &QAction::triggered, this, [this]() {
        moveCurrentDesktopToLeft();
    });
    KGlobalAccel::setGlobalShortcut(actionMoveCurrentDesktopToLeft, QKeySequence());

    actionMoveCurrentDesktopToRight = actionCollection->addAction(QStringLiteral("moveCurrentDesktopToRight"));
    actionMoveCurrentDesktopToRight->setText("Move Current Desktop to Right");
    actionMoveCurrentDesktopToRight->setIcon(QIcon::fromTheme(QStringLiteral("edit-rename")));
    QObject::connect(actionMoveCurrentDesktopToRight, &QAction::triggered, this, [this]() {
        moveCurrentDesktopToRight();
    });
    KGlobalAccel::setGlobalShortcut(actionMoveCurrentDesktopToRight, QKeySequence());
}

const QList<WId> VirtualDesktopBar::getWindows(const int desktopNumber, const bool afterDesktop) {
    QList<WId> windows;

    const QList<WId> allWindows = KWindowSystem::stackingOrder();
    for (WId id : allWindows) {
        if (KWindowSystem::hasWId(id)) {
            const KWindowInfo info = KWindowInfo(id, NET::WMDesktop);
            if (info.desktop() != NET::OnAllDesktops &&
                ((afterDesktop && info.desktop() > desktopNumber) ||
                (!afterDesktop && info.desktop() == desktopNumber))) {
                windows << id;
            }
        }
    }

    return windows;
}

bool VirtualDesktopBar::get_cfg_keepOneEmptyDesktop() {
    return cfg_keepOneEmptyDesktop;
}

void VirtualDesktopBar::set_cfg_keepOneEmptyDesktop(bool value) {
    cfg_keepOneEmptyDesktop = value;
    if (cfg_keepOneEmptyDesktop) {
        if (getEmptyDesktops().length() == 0) {
            addNewDesktop(false);
        } else if (cfg_dropRedundantDesktops) {
            removeEmptyDesktops();
        }
    }
}

bool VirtualDesktopBar::get_cfg_dropRedundantDesktops() {
    return cfg_dropRedundantDesktops;
}

void VirtualDesktopBar::set_cfg_dropRedundantDesktops(bool value) {
    cfg_dropRedundantDesktops = value;
    if (cfg_keepOneEmptyDesktop && cfg_dropRedundantDesktops) {
        removeEmptyDesktops();
    }
}

QString VirtualDesktopBar::get_cfg_emptyDesktopName() {
    return cfg_emptyDesktopName;
}

void VirtualDesktopBar::set_cfg_emptyDesktopName(QString value) {
    cfg_emptyDesktopName = value;
    if (!cfg_emptyDesktopName.isEmpty()) {
        const QList<int> emptyDesktops = getEmptyDesktops();
        renameEmptyDesktops(emptyDesktops);
    }
}

const QList<int> VirtualDesktopBar::getEmptyDesktops() const {
    QList<int> emptyDesktops;

    const int numberOfDesktops = KWindowSystem::numberOfDesktops();
    for (int i = 1; i <= numberOfDesktops; i++) {
        emptyDesktops << i;
    }

    const QList<WId> allWindows = KWindowSystem::windows();
    for (WId id : allWindows) {
        if (KWindowSystem::hasWId(id)) {
            const KWindowInfo info = KWindowInfo(id, NET::WMDesktop | NET::WMState);
            if (!info.hasState(NET::SkipTaskbar) || info.desktop() == NET::OnAllDesktops) {
                emptyDesktops.removeAll(info.desktop());
            }
        }
    }

    return emptyDesktops;
}

void VirtualDesktopBar::renameEmptyDesktops(const QList<int>& emptyDesktops) {
    for (int desktopNumber : emptyDesktops) {
        renameDesktop(desktopNumber, cfg_emptyDesktopName);
    }
}

void VirtualDesktopBar::removeEmptyDesktops() {
    const QList<int> emptyDesktops = getEmptyDesktops();
    if (emptyDesktops.length() <= 1) {
        return;
    }

    dbusInterface.call("invokeShortcut", "VDB-Event-RemoveEmptyDesktops-Before");
    QThread::msleep(100);

    for (int i = emptyDesktops.length() - 1; i >= 1; i--) {
        int emptyDesktopNumber = emptyDesktops[i];
        removeDesktop(emptyDesktopNumber);
    }

    dbusInterface.call("invokeShortcut", "VDB-Event-RemoveEmptyDesktops-After");
}

void VirtualDesktopBar::onWindowAdded(WId id) {
    if (!KWindowSystem::hasWId(id)) {
        return;
    }
    const KWindowInfo info = KWindowInfo(id, NET::WMState);
    if (info.hasState(NET::SkipTaskbar)) {
        return;
    }

    if (cfg_keepOneEmptyDesktop) {
        if (getEmptyDesktops().length() == 0) {
            addNewDesktop(false);
            return;
        }
    }

    const QList<int> emptyDesktops = getEmptyDesktops();
    if (!cfg_emptyDesktopName.isEmpty()) {
        renameEmptyDesktops(emptyDesktops);
    }
    emit emptyDesktopsUpdated(emptyDesktops);
}

void VirtualDesktopBar::onWindowChanged(WId id, NET::Properties properties, NET::Properties2) {
    if (!KWindowSystem::hasWId(id)) {
        return;
    }
    if (properties == 0 || !(properties & NET::WMDesktop)) {
        return;
    }
    const KWindowInfo info = KWindowInfo(id, NET::WMDesktop | NET::WMState | NET::WMWindowType | NET::WMName,
                                         NET::WM2WindowClass);
    if (info.hasState(NET::SkipTaskbar) ||
        (info.windowClassName() == "plasmashell" && info.name() == "Plasma" ) ||
        (info.windowClassName() == "latte-dock" && info.name() == "Latte Dock") ||
        info.windowClassName() == "krunner") {
        return;
    }
    
    if (windowDesktopChangesToIgnore.removeOne(QPair<WId, int>(id, info.desktop()))) {
        if (windowDesktopChangesToIgnore.isEmpty()) {
            const QList<int> emptyDesktops = getEmptyDesktops();
            if (!cfg_emptyDesktopName.isEmpty()) {
                renameEmptyDesktops(emptyDesktops);
            }
            emit emptyDesktopsUpdated(emptyDesktops);
        }
        return;
    }

    if (cfg_keepOneEmptyDesktop) {
        int numberOfEmptyDesktops = getEmptyDesktops().length();
        if (numberOfEmptyDesktops == 0) {
            addNewDesktop(false);
            return;
        } else if (numberOfEmptyDesktops > 1 && cfg_dropRedundantDesktops) {
            removeEmptyDesktops();
            return;
        }
    }

    const QList<int> emptyDesktops = getEmptyDesktops();
    if (!cfg_emptyDesktopName.isEmpty()) {
        renameEmptyDesktops(emptyDesktops);
    }
    emit emptyDesktopsUpdated(emptyDesktops);
}

void VirtualDesktopBar::onWindowRemoved(WId id) {
    const KWindowInfo info = KWindowInfo(id, NET::WMState | NET::WMWindowType | NET::WMName,
                                         NET::WM2WindowClass);
    if (info.hasState(NET::SkipTaskbar) ||
        (info.windowClassName() == "plasmashell" && info.name() == "Plasma" ) ||
        (info.windowClassName() == "latte-dock" && info.name() == "Latte Dock") ||
        info.windowClassName() == "krunner") {
        return;
    }

    if (cfg_keepOneEmptyDesktop && cfg_dropRedundantDesktops) {
        if (getEmptyDesktops().length() > 1) {
            removeEmptyDesktops();
            return;
        }
    }

    const QList<int> emptyDesktops = getEmptyDesktops();
    if (!cfg_emptyDesktopName.isEmpty()) {
        renameEmptyDesktops(emptyDesktops);
    }
    emit emptyDesktopsUpdated(emptyDesktops);
}
