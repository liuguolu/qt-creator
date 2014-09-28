/****************************************************************************
**
** Copyright (C) 2014 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
****************************************************************************/

#include "defaultpropertyprovider.h"
#include "qbsconstants.h"

#include <projectexplorer/kit.h>
#include <projectexplorer/kitinformation.h>
#include <projectexplorer/toolchain.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <qtsupport/baseqtversion.h>
#include <utils/qtcassert.h>

#include <tools/hostosinfo.h>

#include <ios/iosconstants.h>
#include <qnx/qnxconstants.h>
#include <winrt/winrtconstants.h>

#include <QDir>
#include <QFileInfo>

namespace QbsProjectManager {
using namespace Constants;
using namespace ProjectExplorer::Constants;
using namespace Ios::Constants;
using namespace Qnx::Constants;
using namespace WinRt::Internal::Constants;

static QString extractToolchainPrefix(QString *compilerName)
{
    QString prefix;
    if (compilerName->endsWith(QLatin1String("-g++"))
            || compilerName->endsWith(QLatin1String("-clang++"))) {
        const int idx = compilerName->lastIndexOf(QLatin1Char('-')) + 1;
        prefix = compilerName->left(idx);
        compilerName->remove(0, idx);
    }
    return prefix;
}

static QStringList targetOSList(const ProjectExplorer::Abi &abi, const ProjectExplorer::Kit *k)
{
    const Core::Id device = ProjectExplorer::DeviceTypeKitInformation::deviceTypeId(k);
    QStringList os;
    switch (abi.os()) {
    case ProjectExplorer::Abi::WindowsOS:
        if (device == WINRT_DEVICE_TYPE_LOCAL ||
                device == WINRT_DEVICE_TYPE_PHONE ||
                device == WINRT_DEVICE_TYPE_EMULATOR) {
            os << QLatin1String("winrt");
        } else if (abi.osFlavor() == ProjectExplorer::Abi::WindowsCEFlavor) {
            os << QLatin1String("windowsce");
        }
        os << QLatin1String("windows");
        break;
    case ProjectExplorer::Abi::MacOS:
        if (device == DESKTOP_DEVICE_TYPE)
            os << QLatin1String("osx");
        else if (device == IOS_DEVICE_TYPE)
            os << QLatin1String("ios");
        else if (device == IOS_SIMULATOR_TYPE)
            os << QLatin1String("ios-simulator") << QLatin1String("ios");
        os << QLatin1String("darwin") << QLatin1String("bsd") << QLatin1String("unix");
        break;
    case ProjectExplorer::Abi::LinuxOS:
        if (abi.osFlavor() == ProjectExplorer::Abi::AndroidLinuxFlavor)
            os << QLatin1String("android");
        os << QLatin1String("linux") << QLatin1String("unix");
        break;
    case ProjectExplorer::Abi::BsdOS:
        switch (abi.osFlavor()) {
        case ProjectExplorer::Abi::FreeBsdFlavor:
            os << QLatin1String("freebsd");
            break;
        case ProjectExplorer::Abi::NetBsdFlavor:
            os << QLatin1String("netbsd");
            break;
        case ProjectExplorer::Abi::OpenBsdFlavor:
            os << QLatin1String("openbsd");
            break;
        default:
            break;
        }
        os << QLatin1String("bsd") << QLatin1String("unix");
        break;
    case ProjectExplorer::Abi::UnixOS:
        if (device == QNX_BB_OS_TYPE)
            os << QLatin1String("blackberry") << QLatin1String("qnx");
        else if (device == QNX_QNX_OS_TYPE)
            os << QLatin1String("qnx");
        else if (abi.osFlavor() == ProjectExplorer::Abi::SolarisUnixFlavor)
            os << QLatin1String("solaris");
        os << QLatin1String("unix");
        break;
    default:
        break;
    }
    return os;
}

static QStringList toolchainList(const ProjectExplorer::ToolChain *tc)
{
    QStringList list;
    if (tc->type() == QLatin1String("clang"))
        list << QLatin1String("clang") << QLatin1String("llvm") << QLatin1String("gcc");
    else if (tc->type() == QLatin1String("gcc"))
        list << QLatin1String("gcc"); // TODO: Detect llvm-gcc
    else if (tc->type() == QLatin1String("mingw"))
        list << QLatin1String("mingw") << QLatin1String("gcc");
    else if (tc->type() == QLatin1String("msvc"))
        list << QLatin1String("msvc");
    return list;
}

QVariantMap DefaultPropertyProvider::properties(const ProjectExplorer::Kit *k, const QVariantMap &defaultData) const
{
    QTC_ASSERT(k, return defaultData);
    QVariantMap data = defaultData;

    const QString sysroot = ProjectExplorer::SysRootKitInformation::sysRoot(k).toUserOutput();
    if (ProjectExplorer::SysRootKitInformation::hasSysRoot(k))
        data.insert(QLatin1String(QBS_SYSROOT), sysroot);

    ProjectExplorer::ToolChain *tc = ProjectExplorer::ToolChainKitInformation::toolChain(k);
    if (!tc)
        return data;

    ProjectExplorer::Abi targetAbi = tc->targetAbi();
    if (targetAbi.architecture() != ProjectExplorer::Abi::UnknownArchitecture) {
        QString architecture = ProjectExplorer::Abi::toString(targetAbi.architecture());

        // We have to be conservative tacking on suffixes to arch names because an arch that is
        // already 64-bit may get an incorrect name as a result (i.e. Itanium)
        if (targetAbi.wordWidth() == 64) {
            switch (targetAbi.architecture()) {
            case ProjectExplorer::Abi::X86Architecture:
                architecture.append(QLatin1Char('_'));
                // fall through
            case ProjectExplorer::Abi::ArmArchitecture:
            case ProjectExplorer::Abi::MipsArchitecture:
            case ProjectExplorer::Abi::PowerPCArchitecture:
                architecture.append(QString::number(targetAbi.wordWidth()));
                break;
            default:
                break;
            }
        }

        data.insert(QLatin1String(QBS_ARCHITECTURE),
                    qbs::Internal::HostOsInfo::canonicalArchitecture(architecture));
    }

    QStringList targetOS = targetOSList(targetAbi, k);
    if (!targetOS.isEmpty())
        data.insert(QLatin1String(QBS_TARGETOS), targetOS);

    QStringList toolchain = toolchainList(tc);
    if (!toolchain.isEmpty())
        data.insert(QLatin1String(QBS_TOOLCHAIN), toolchain);

    if (targetAbi.os() == ProjectExplorer::Abi::MacOS) {
        // Set Xcode SDK name and version - required by Qbs if a sysroot is present
        // Ideally this would be done in a better way...
        QRegExp re(QLatin1String("(MacOSX|iPhoneOS|iPhoneSimulator)([0-9]+\\.[0-9]+)\\.sdk"));
        if (re.exactMatch(QDir(sysroot).dirName())) {
            data.insert(QLatin1String(CPP_XCODESDKNAME), QString(re.cap(1).toLower() + re.cap(2)));
            data.insert(QLatin1String(CPP_XCODESDKVERSION), re.cap(2));
        }
    }

    Utils::FileName cxx = tc->compilerCommand();
    const QFileInfo cxxFileInfo = cxx.toFileInfo();
    QString compilerName = cxxFileInfo.fileName();
    const QString toolchainPrefix = extractToolchainPrefix(&compilerName);
    if (!toolchainPrefix.isEmpty())
        data.insert(QLatin1String(CPP_TOOLCHAINPREFIX), toolchainPrefix);
    data.insert(QLatin1String(CPP_COMPILERNAME), compilerName);
    if (targetAbi.os() != ProjectExplorer::Abi::WindowsOS
            || targetAbi.osFlavor() == ProjectExplorer::Abi::WindowsMSysFlavor) {
        data.insert(QLatin1String(CPP_LINKERNAME), compilerName);
    }
    data.insert(QLatin1String(CPP_TOOLCHAINPATH), cxxFileInfo.absolutePath());
    if (targetAbi.osFlavor() == ProjectExplorer::Abi::WindowsMsvc2013Flavor) {
        const QLatin1String flags("/FS");
        data.insert(QLatin1String(CPP_PLATFORMCFLAGS), flags);
        data.insert(QLatin1String(CPP_PLATFORMCXXFLAGS), flags);
    }
    return data;
}

} // namespace QbsProjectManager
