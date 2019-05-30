// This file is part of IpFreely application.
//
// Copyright (C) 2018, Duncan Crutchley
// Contact <dac1976github@outlook.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License and GNU Lesser General Public License
// for more details.
//
// You should have received a copy of the GNU General Public License
// and GNU Lesser General Public License along with this program. If
// not, see <http://www.gnu.org/licenses/>.

/*!
 * \file IpFreelyMainWindow.cpp
 * \brief File containing definition of IpFreelyMainWindow form.
 */
#include "IpFreelyMainWindow.h"
#include "ui_IpFreelyMainWindow.h"
#include <QTimer>
#include <QToolButton>
#include <QResizeEvent>
#include <QCloseEvent>
#include <QMessageBox>
#include <QLayout>
#include <QLayoutItem>
#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QScreen>
#include <QRectF>
#include <stdexcept>
#include <string>
#include <ctime>
#include <set>
#include <boost/filesystem.hpp>
#include "IpFreelyVideoFrame.h"
#include "IpFreelyVideoForm.h"
#include "IpFreelyPreferencesDialog.h"
#include "IpFreelyAbout.h"
#include "IpFreelyCameraSetupDialog.h"
#include "IpFreelySdCardViewerDialog.h"
#include "IpFreelyStreamProcessor.h"
#include "IpFreelyDiskSpaceManager.h"
#include "StringUtils/StringUtils.h"
#include "DebugLog/DebugLogging.h"

namespace bfs = boost::filesystem;

namespace
{

static constexpr int DEFAULT_UPDATE_PERIOD_MS = 100;

void ClearLayout(QLayout* layout, bool deleteWidgets)
{
    if (!layout)
    {
        return;
    }

    while (layout->count() > 0)
    {
        QLayoutItem* item = layout->takeAt(0);

        if (item)
        {
            QWidget* widget = item->widget();

            if (widget)
            {
                widget->setParent(nullptr);

                if (deleteWidgets)
                {
                    delete widget;
                }
            }

            delete item;
        }
    }
}

} // namespace

IpFreelyMainWindow::IpFreelyMainWindow(QString const& appVersion, QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::IpFreelyMainWindow)
    , m_appVersion(appVersion)
    , m_updateFeedsTimer(new QTimer(this))
    , m_numConnections(0)
    , m_videoForm(std::make_shared<IpFreelyVideoForm>())
    , m_diskSpaceMgr(std::make_shared<ipfreely::IpFreelyDiskSpaceManager>(
          m_prefs.SaveFolderPath(), m_prefs.MaxNumDaysData(), m_prefs.MaxUsedDiskSpacePercent()))
{
    ui->setupUi(this);

    connect(m_updateFeedsTimer, &QTimer::timeout, this, &IpFreelyMainWindow::on_updateFeedsTimer);

    SetDisplaySize();

    ConnectButtons();

    ui->cam1RemoveMotionRegionsToolButton->setVisible(false);
    ui->cam2RemoveMotionRegionsToolButton->setVisible(false);
    ui->cam3RemoveMotionRegionsToolButton->setVisible(false);
    ui->cam4RemoveMotionRegionsToolButton->setVisible(false);

    QTimer::singleShot(100, this, &IpFreelyMainWindow::CheckStartupConnections);
}

IpFreelyMainWindow::~IpFreelyMainWindow()
{
    delete ui;
}

void IpFreelyMainWindow::on_actionClose_triggered()
{
    QApplication::quit();
}

void IpFreelyMainWindow::on_actionPreferences_triggered()
{
    IpFreelyPreferencesDialog prefsDlg(m_prefs);
    prefsDlg.setModal(true);

    if (prefsDlg.exec() == QDialog::Rejected)
    {
        return;
    }

    std::set<ipfreely::eCamId> camIds;

    for (auto const& camFeed : m_camFeeds)
    {
        camIds.emplace(camFeed.first);
    }

    // Disconnect from feeds so we pick up changes to prefs when we reconnect.
    for (auto const& camId : camIds)
    {
        switch (camId)
        {
        case ipfreely::eCamId::cam1:
            on_connect1ToolButton_clicked();
            break;
        case ipfreely::eCamId::cam2:
            on_connect2ToolButton_clicked();
            break;
        case ipfreely::eCamId::cam3:
            on_connect3ToolButton_clicked();
            break;
        case ipfreely::eCamId::cam4:
            on_connect4ToolButton_clicked();
            break;
        case ipfreely::eCamId::noCam:
            // Do nothing.
            break;
        }
    }

    // Reconnect to cameras that were previsouly running before changing preferences.
    for (auto const& camId : camIds)
    {
        switch (camId)
        {
        case ipfreely::eCamId::cam1:
            on_connect1ToolButton_clicked();
            break;
        case ipfreely::eCamId::cam2:
            on_connect2ToolButton_clicked();
            break;
        case ipfreely::eCamId::cam3:
            on_connect3ToolButton_clicked();
            break;
        case ipfreely::eCamId::cam4:
            on_connect4ToolButton_clicked();
            break;
        case ipfreely::eCamId::noCam:
            // Do nothing.
            break;
        }
    }

    // Recreate disk space manager.
    m_diskSpaceMgr.reset();
    m_diskSpaceMgr = std::make_shared<ipfreely::IpFreelyDiskSpaceManager>(
        m_prefs.SaveFolderPath(), m_prefs.MaxNumDaysData(), m_prefs.MaxUsedDiskSpacePercent());
}

void IpFreelyMainWindow::on_actionAbout_triggered()
{
    IpFreelyAbout aboutDlg;
    aboutDlg.setModal(true);
    QString title = tr("IP Freely (IP/Web camera stream viewer and recorder)") + " " + m_appVersion;
    aboutDlg.SetTitle(title);
    aboutDlg.exec();
}

void IpFreelyMainWindow::on_settings1ToolButton_clicked()
{
    auto streamProcIter = m_streamProcessors.find(ipfreely::eCamId::cam1);
    bool reconnect      = false;

    if (m_camDb.DoesCameraExist(ipfreely::eCamId::cam1) &&
        (streamProcIter != m_streamProcessors.end()))
    {
        on_connect1ToolButton_clicked();
        reconnect = true;
    }

    SetupCameraInDb(ipfreely::eCamId::cam1, ui->cam1ConnectToolButton);

    if (ui->cam1ConnectToolButton->isEnabled() && reconnect)
    {
        on_connect1ToolButton_clicked();
    }
}

void IpFreelyMainWindow::on_connect1ToolButton_clicked()
{
    ipfreely::IpCamera camera;

    if (!m_camDb.FindCamera(ipfreely::eCamId::cam1, camera))
    {
        DEBUG_MESSAGE_EX_ERROR(
            "Failed to find camera, ID: " << static_cast<int>(ipfreely::eCamId::cam1));
        return;
    }

    ConnectionHandler(camera,
                      ui->cam1ConnectToolButton,
                      ui->cam1MotionRegionsToolButton,
                      ui->cam1RemoveMotionRegionsToolButton,
                      ui->cam1RecordToolButton,
                      ui->cam1ImageToolButton,
                      ui->cam1ExpandToolButton,
                      ui->cam1StorageToolButton);
}

void IpFreelyMainWindow::on_motionDetectorRegions1ToolButton_toggled(bool checked)
{
    EnableMotionRegionsSetup(ipfreely::eCamId::cam1,
                             checked,
                             ui->cam1RemoveMotionRegionsToolButton,
                             ui->cam1RemoveMotionRegionsToolButton);
}

void IpFreelyMainWindow::on_removeMotionRegions1ToolButton_clicked()
{
    RemoveMotionRegions(ipfreely::eCamId::cam1);
}

void IpFreelyMainWindow::on_record1ToolButton_clicked()
{
    RecordActionHandler(ipfreely::eCamId::cam1, ui->cam1RecordToolButton);
}

void IpFreelyMainWindow::on_snapshot1ToolButton_clicked()
{
    SaveImageSnapshot(ipfreely::eCamId::cam1);
}

void IpFreelyMainWindow::on_expand1ToolButton_clicked()
{
    ShowExpandedVideoForm(ipfreely::eCamId::cam1);
}

void IpFreelyMainWindow::on_storage1ToolButton_clicked()
{
    ipfreely::IpCamera camera;

    if (!m_camDb.FindCamera(ipfreely::eCamId::cam1, camera))
    {
        DEBUG_MESSAGE_EX_ERROR(
            "Failed to find camera, ID: " << static_cast<int>(ipfreely::eCamId::cam1));
        return;
    }

    ViewStorage(camera);
}

void IpFreelyMainWindow::on_settings2ToolButton_clicked()
{
    auto streamProcIter = m_streamProcessors.find(ipfreely::eCamId::cam2);
    bool reconnect      = false;

    if (m_camDb.DoesCameraExist(ipfreely::eCamId::cam2) &&
        (streamProcIter != m_streamProcessors.end()))
    {
        on_connect2ToolButton_clicked();
        reconnect = true;
    }

    SetupCameraInDb(ipfreely::eCamId::cam2, ui->cam2ConnectToolButton);

    if (ui->cam2ConnectToolButton->isEnabled() && reconnect)
    {
        on_connect2ToolButton_clicked();
    }
}

void IpFreelyMainWindow::on_connect2ToolButton_clicked()
{
    ipfreely::IpCamera camera;

    if (!m_camDb.FindCamera(ipfreely::eCamId::cam2, camera))
    {
        DEBUG_MESSAGE_EX_ERROR(
            "Failed to find camera, ID: " << static_cast<int>(ipfreely::eCamId::cam2));
        return;
    }

    ConnectionHandler(camera,
                      ui->cam2ConnectToolButton,
                      ui->cam2MotionRegionsToolButton,
                      ui->cam2RemoveMotionRegionsToolButton,
                      ui->cam2RecordToolButton,
                      ui->cam2ImageToolButton,
                      ui->cam2ExpandToolButton,
                      ui->cam2StorageToolButton);
}

void IpFreelyMainWindow::on_motionDetectorRegions2ToolButton_toggled(bool checked)
{
    EnableMotionRegionsSetup(ipfreely::eCamId::cam2,
                             checked,
                             ui->cam2RemoveMotionRegionsToolButton,
                             ui->cam2RemoveMotionRegionsToolButton);
}

void IpFreelyMainWindow::on_removeMotionRegions2ToolButton_clicked()
{
    RemoveMotionRegions(ipfreely::eCamId::cam2);
}

void IpFreelyMainWindow::on_record2ToolButton_clicked()
{
    RecordActionHandler(ipfreely::eCamId::cam2, ui->cam2RecordToolButton);
}

void IpFreelyMainWindow::on_snapshot2ToolButton_clicked()
{
    SaveImageSnapshot(ipfreely::eCamId::cam2);
}

void IpFreelyMainWindow::on_expand2ToolButton_clicked()
{
    ShowExpandedVideoForm(ipfreely::eCamId::cam2);
}

void IpFreelyMainWindow::on_storage2ToolButton_clicked()
{
    ipfreely::IpCamera camera;

    if (!m_camDb.FindCamera(ipfreely::eCamId::cam2, camera))
    {
        DEBUG_MESSAGE_EX_ERROR(
            "Failed to find camera, ID: " << static_cast<int>(ipfreely::eCamId::cam2));
        return;
    }

    ViewStorage(camera);
}

void IpFreelyMainWindow::on_settings3ToolButton_clicked()
{
    auto streamProcIter = m_streamProcessors.find(ipfreely::eCamId::cam3);
    bool reconnect      = false;

    if (m_camDb.DoesCameraExist(ipfreely::eCamId::cam3) &&
        (streamProcIter != m_streamProcessors.end()))
    {
        on_connect3ToolButton_clicked();
        reconnect = true;
    }

    SetupCameraInDb(ipfreely::eCamId::cam3, ui->cam3ConnectToolButton);

    if (ui->cam3ConnectToolButton->isEnabled() && reconnect)
    {
        on_connect3ToolButton_clicked();
    }
}

void IpFreelyMainWindow::on_connect3ToolButton_clicked()
{
    ipfreely::IpCamera camera;

    if (!m_camDb.FindCamera(ipfreely::eCamId::cam3, camera))
    {
        DEBUG_MESSAGE_EX_ERROR(
            "Failed to find camera, ID: " << static_cast<int>(ipfreely::eCamId::cam3));
        return;
    }

    ConnectionHandler(camera,
                      ui->cam3ConnectToolButton,
                      ui->cam3MotionRegionsToolButton,
                      ui->cam3RemoveMotionRegionsToolButton,
                      ui->cam3RecordToolButton,
                      ui->cam3ImageToolButton,
                      ui->cam3ExpandToolButton,
                      ui->cam3StorageToolButton);
}

void IpFreelyMainWindow::on_motionDetectorRegions3ToolButton_toggled(bool checked)
{
    EnableMotionRegionsSetup(ipfreely::eCamId::cam3,
                             checked,
                             ui->cam3RemoveMotionRegionsToolButton,
                             ui->cam3RemoveMotionRegionsToolButton);
}

void IpFreelyMainWindow::on_removeMotionRegions3ToolButton_clicked()
{
    RemoveMotionRegions(ipfreely::eCamId::cam3);
}

void IpFreelyMainWindow::on_record3ToolButton_clicked()
{
    RecordActionHandler(ipfreely::eCamId::cam3, ui->cam3RecordToolButton);
}

void IpFreelyMainWindow::on_snapshot3ToolButton_clicked()
{
    SaveImageSnapshot(ipfreely::eCamId::cam3);
}

void IpFreelyMainWindow::on_expand3ToolButton_clicked()
{
    ShowExpandedVideoForm(ipfreely::eCamId::cam3);
}

void IpFreelyMainWindow::on_storage3ToolButton_clicked()
{
    ipfreely::IpCamera camera;

    if (!m_camDb.FindCamera(ipfreely::eCamId::cam3, camera))
    {
        DEBUG_MESSAGE_EX_ERROR(
            "Failed to find camera, ID: " << static_cast<int>(ipfreely::eCamId::cam3));
        return;
    }

    ViewStorage(camera);
}

void IpFreelyMainWindow::on_settings4ToolButton_clicked()
{
    auto streamProcIter = m_streamProcessors.find(ipfreely::eCamId::cam4);
    bool reconnect      = false;

    if (m_camDb.DoesCameraExist(ipfreely::eCamId::cam4) &&
        (streamProcIter != m_streamProcessors.end()))
    {
        on_connect4ToolButton_clicked();
        reconnect = true;
    }

    SetupCameraInDb(ipfreely::eCamId::cam4, ui->cam4ConnectToolButton);

    if (ui->cam4ConnectToolButton->isEnabled() && reconnect)
    {
        on_connect4ToolButton_clicked();
    }
}

void IpFreelyMainWindow::on_connect4ToolButton_clicked()
{
    ipfreely::IpCamera camera;

    if (!m_camDb.FindCamera(ipfreely::eCamId::cam4, camera))
    {
        DEBUG_MESSAGE_EX_ERROR(
            "Failed to find camera, ID: " << static_cast<int>(ipfreely::eCamId::cam4));
        return;
    }

    ConnectionHandler(camera,
                      ui->cam4ConnectToolButton,
                      ui->cam4MotionRegionsToolButton,
                      ui->cam4RemoveMotionRegionsToolButton,
                      ui->cam4RecordToolButton,
                      ui->cam4ImageToolButton,
                      ui->cam4ExpandToolButton,
                      ui->cam4StorageToolButton);
}

void IpFreelyMainWindow::on_motionDetectorRegions4ToolButton_toggled(bool checked)
{
    EnableMotionRegionsSetup(ipfreely::eCamId::cam4,
                             checked,
                             ui->cam4RemoveMotionRegionsToolButton,
                             ui->cam4RemoveMotionRegionsToolButton);
}

void IpFreelyMainWindow::on_removeMotionRegions4ToolButton_clicked()
{
    RemoveMotionRegions(ipfreely::eCamId::cam4);
}

void IpFreelyMainWindow::on_record4ToolButton_clicked()
{
    RecordActionHandler(ipfreely::eCamId::cam4, ui->cam4RecordToolButton);
}

void IpFreelyMainWindow::on_snapshot4ToolButton_clicked()
{
    SaveImageSnapshot(ipfreely::eCamId::cam4);
}

void IpFreelyMainWindow::on_expand4ToolButton_clicked()
{
    ShowExpandedVideoForm(ipfreely::eCamId::cam4);
}

void IpFreelyMainWindow::on_storage4ToolButton_clicked()
{
    ipfreely::IpCamera camera;

    if (!m_camDb.FindCamera(ipfreely::eCamId::cam4, camera))
    {
        DEBUG_MESSAGE_EX_ERROR(
            "Failed to find camera, ID: " << static_cast<int>(ipfreely::eCamId::cam4));
        return;
    }

    ViewStorage(camera);
}

void IpFreelyMainWindow::on_updateFeedsTimer()
{
    for (auto const& streamProcessor : m_streamProcessors)
    {
        if (streamProcessor.second->VideoFrameUpdated())
        {
            QRect motionBoundingRect;
            auto currentVideoFrame = streamProcessor.second->CurrentVideoFrame(&motionBoundingRect);

            auto originalFps = streamProcessor.second->OriginalFps();
            auto fps         = streamProcessor.second->CurrentFps();
            auto isRecording = streamProcessor.second->VideoWritingEnabled();

            UpdateCamFeedFrame(
                streamProcessor.first, currentVideoFrame, motionBoundingRect, isRecording);

            SetFpsInTitle(streamProcessor.first, fps, originalFps);

            if (m_videoForm->isVisible() && (m_videoFormId == streamProcessor.first))
            {
                ipfreely::IpCamera::regions_t motionRegions;

                if (m_motionAreaSetupEnabled[streamProcessor.first])
                {
                    motionRegions = m_camMotionRegions[streamProcessor.first];
                }

                m_videoForm->SetVideoFrame(currentVideoFrame,
                                           fps,
                                           originalFps,
                                           motionBoundingRect,
                                           isRecording,
                                           motionRegions);
            }
        }
    }
}

void IpFreelyMainWindow::closeEvent(QCloseEvent* event)
{
    if (m_videoForm->isVisible())
    {
        m_videoForm->close();
    }

    QMainWindow::closeEvent(event);
}

void IpFreelyMainWindow::resizeEvent(QResizeEvent* event)
{
    if (m_camFeeds.count(ipfreely::eCamId::cam1) > 0)
    {
        ClearLayout(ui->cam1Widget->layout(), true);
        m_camFeeds.erase(ipfreely::eCamId::cam1);
        auto feed = new IpFreelyVideoFrame(1,
                                           std::bind(&IpFreelyMainWindow::VideoFrameAreaSelection,
                                                     this,
                                                     std::placeholders::_1,
                                                     std::placeholders::_2),
                                           this);
        feed->SetEnableSelection(m_motionAreaSetupEnabled[ipfreely::eCamId::cam1]);
        ui->cam1Widget->layout()->addWidget(feed);
        m_camFeeds[ipfreely::eCamId::cam1] = feed;
    }

    if (m_camFeeds.count(ipfreely::eCamId::cam2) > 0)
    {
        ClearLayout(ui->cam2Widget->layout(), true);
        m_camFeeds.erase(ipfreely::eCamId::cam2);
        auto feed = new IpFreelyVideoFrame(2,
                                           std::bind(&IpFreelyMainWindow::VideoFrameAreaSelection,
                                                     this,
                                                     std::placeholders::_1,
                                                     std::placeholders::_2),
                                           this);
        feed->SetEnableSelection(m_motionAreaSetupEnabled[ipfreely::eCamId::cam2]);
        ui->cam2Widget->layout()->addWidget(feed);
        m_camFeeds[ipfreely::eCamId::cam2] = feed;
    }

    if (m_camFeeds.count(ipfreely::eCamId::cam3) > 0)
    {
        ClearLayout(ui->cam3Widget->layout(), true);
        m_camFeeds.erase(ipfreely::eCamId::cam3);
        auto feed = new IpFreelyVideoFrame(3,
                                           std::bind(&IpFreelyMainWindow::VideoFrameAreaSelection,
                                                     this,
                                                     std::placeholders::_1,
                                                     std::placeholders::_2),
                                           this);
        feed->SetEnableSelection(m_motionAreaSetupEnabled[ipfreely::eCamId::cam3]);
        ui->cam3Widget->layout()->addWidget(feed);
        m_camFeeds[ipfreely::eCamId::cam3] = feed;
    }

    if (m_camFeeds.count(ipfreely::eCamId::cam4) > 0)
    {
        ClearLayout(ui->cam4Widget->layout(), true);
        m_camFeeds.erase(ipfreely::eCamId::cam4);
        auto feed = new IpFreelyVideoFrame(4,
                                           std::bind(&IpFreelyMainWindow::VideoFrameAreaSelection,
                                                     this,
                                                     std::placeholders::_1,
                                                     std::placeholders::_2),
                                           this);
        feed->SetEnableSelection(m_motionAreaSetupEnabled[ipfreely::eCamId::cam4]);
        ui->cam4Widget->layout()->addWidget(feed);
        m_camFeeds[ipfreely::eCamId::cam4] = feed;
    }

    QMainWindow::resizeEvent(event);
}

void IpFreelyMainWindow::SetDisplaySize()
{
    static constexpr double DEFAULT_SCREEN_SIZE = 1080.0;
    static constexpr int    MIN_BUTTON_SIZE     = 24;
    static constexpr int    MAX_BUTTON_SIZE     = 48;
    static constexpr int    MIN_DISPLAY_WIDTH   = 800;
    static constexpr int    MIN_DISPLAY_HEIGHT  = 600;

    auto      displayGeometry = geometry();
    auto      screenPos       = mapToGlobal(QPoint(displayGeometry.left(), displayGeometry.top()));
    auto      screen          = qApp->screenAt(screenPos);
    double    scaleFactor     = static_cast<double>(screen->size().height()) / DEFAULT_SCREEN_SIZE;
    int const maxDisplayWidth = static_cast<int>(static_cast<double>(screen->size().width()) * 0.9);
    int const maxDisplayHeight =
        static_cast<int>(static_cast<double>(screen->size().height()) * 0.9);

    int displayWidth = static_cast<int>(static_cast<double>(displayGeometry.width()) * scaleFactor);

    if (displayWidth < MIN_DISPLAY_WIDTH)
    {
        displayWidth = MIN_DISPLAY_WIDTH;
    }
    else if (displayWidth > maxDisplayWidth)
    {
        displayWidth = maxDisplayWidth;
    }

    int displayHeight =
        static_cast<int>(static_cast<double>(displayGeometry.height()) * scaleFactor);

    if (displayHeight < MIN_DISPLAY_HEIGHT)
    {
        displayHeight = MIN_DISPLAY_HEIGHT;
    }
    else if (displayHeight > maxDisplayHeight)
    {
        displayHeight = maxDisplayHeight;
    }

    int displayLeft =
        static_cast<int>(static_cast<double>(screen->size().width() - displayWidth) / 2.0);

    int displayTop =
        static_cast<int>(static_cast<double>(screen->size().height() - displayHeight) / 2.0);

    displayGeometry.setTop(displayTop);
    displayGeometry.setLeft(displayLeft);
    displayGeometry.setWidth(displayWidth);
    displayGeometry.setHeight(displayHeight);
    setGeometry(displayGeometry);

    auto buttonGeometry = ui->cam1SettingsToolButton->geometry();
    int  buttonSize = static_cast<int>(static_cast<double>(buttonGeometry.height()) * scaleFactor);

    if (buttonSize < MIN_BUTTON_SIZE)
    {
        buttonSize = MIN_BUTTON_SIZE;
    }
    else if (buttonSize > MAX_BUTTON_SIZE)
    {
        buttonSize = MAX_BUTTON_SIZE;
    }

    ui->cam1SettingsToolButton->setMinimumSize(QSize(buttonSize, buttonSize));
    ui->cam1ConnectToolButton->setMinimumSize(QSize(buttonSize, buttonSize));
    ui->cam1MotionRegionsToolButton->setMinimumSize(QSize(buttonSize, buttonSize));
    ui->cam1RemoveMotionRegionsToolButton->setMinimumSize(QSize(buttonSize, buttonSize));
    ui->cam1ImageToolButton->setMinimumSize(QSize(buttonSize, buttonSize));
    ui->cam1RecordToolButton->setMinimumSize(QSize(buttonSize, buttonSize));
    ui->cam1ExpandToolButton->setMinimumSize(QSize(buttonSize, buttonSize));
    ui->cam1StorageToolButton->setMinimumSize(QSize(buttonSize, buttonSize));

    ui->cam4SettingsToolButton->setMinimumSize(QSize(buttonSize, buttonSize));
    ui->cam4ConnectToolButton->setMinimumSize(QSize(buttonSize, buttonSize));
    ui->cam4MotionRegionsToolButton->setMinimumSize(QSize(buttonSize, buttonSize));
    ui->cam4RemoveMotionRegionsToolButton->setMinimumSize(QSize(buttonSize, buttonSize));
    ui->cam4ImageToolButton->setMinimumSize(QSize(buttonSize, buttonSize));
    ui->cam4RecordToolButton->setMinimumSize(QSize(buttonSize, buttonSize));
    ui->cam4ExpandToolButton->setMinimumSize(QSize(buttonSize, buttonSize));
    ui->cam4StorageToolButton->setMinimumSize(QSize(buttonSize, buttonSize));

    ui->cam2SettingsToolButton->setMinimumSize(QSize(buttonSize, buttonSize));
    ui->cam2ConnectToolButton->setMinimumSize(QSize(buttonSize, buttonSize));
    ui->cam2MotionRegionsToolButton->setMinimumSize(QSize(buttonSize, buttonSize));
    ui->cam2RemoveMotionRegionsToolButton->setMinimumSize(QSize(buttonSize, buttonSize));
    ui->cam2ImageToolButton->setMinimumSize(QSize(buttonSize, buttonSize));
    ui->cam2RecordToolButton->setMinimumSize(QSize(buttonSize, buttonSize));
    ui->cam2ExpandToolButton->setMinimumSize(QSize(buttonSize, buttonSize));
    ui->cam2StorageToolButton->setMinimumSize(QSize(buttonSize, buttonSize));

    ui->cam3SettingsToolButton->setMinimumSize(QSize(buttonSize, buttonSize));
    ui->cam3ConnectToolButton->setMinimumSize(QSize(buttonSize, buttonSize));
    ui->cam3MotionRegionsToolButton->setMinimumSize(QSize(buttonSize, buttonSize));
    ui->cam3RemoveMotionRegionsToolButton->setMinimumSize(QSize(buttonSize, buttonSize));
    ui->cam3ImageToolButton->setMinimumSize(QSize(buttonSize, buttonSize));
    ui->cam3RecordToolButton->setMinimumSize(QSize(buttonSize, buttonSize));
    ui->cam3ExpandToolButton->setMinimumSize(QSize(buttonSize, buttonSize));
    ui->cam3StorageToolButton->setMinimumSize(QSize(buttonSize, buttonSize));

    ui->cam1SettingsToolButton->setMaximumSize(QSize(buttonSize, buttonSize));
    ui->cam1ConnectToolButton->setMaximumSize(QSize(buttonSize, buttonSize));
    ui->cam1MotionRegionsToolButton->setMaximumSize(QSize(buttonSize, buttonSize));
    ui->cam1RemoveMotionRegionsToolButton->setMaximumSize(QSize(buttonSize, buttonSize));
    ui->cam1ImageToolButton->setMaximumSize(QSize(buttonSize, buttonSize));
    ui->cam1RecordToolButton->setMaximumSize(QSize(buttonSize, buttonSize));
    ui->cam1ExpandToolButton->setMaximumSize(QSize(buttonSize, buttonSize));
    ui->cam1StorageToolButton->setMaximumSize(QSize(buttonSize, buttonSize));

    ui->cam4SettingsToolButton->setMaximumSize(QSize(buttonSize, buttonSize));
    ui->cam4ConnectToolButton->setMaximumSize(QSize(buttonSize, buttonSize));
    ui->cam4MotionRegionsToolButton->setMaximumSize(QSize(buttonSize, buttonSize));
    ui->cam4RemoveMotionRegionsToolButton->setMaximumSize(QSize(buttonSize, buttonSize));
    ui->cam4ImageToolButton->setMaximumSize(QSize(buttonSize, buttonSize));
    ui->cam4RecordToolButton->setMaximumSize(QSize(buttonSize, buttonSize));
    ui->cam4ExpandToolButton->setMaximumSize(QSize(buttonSize, buttonSize));
    ui->cam4StorageToolButton->setMaximumSize(QSize(buttonSize, buttonSize));

    ui->cam2SettingsToolButton->setMaximumSize(QSize(buttonSize, buttonSize));
    ui->cam2ConnectToolButton->setMaximumSize(QSize(buttonSize, buttonSize));
    ui->cam2MotionRegionsToolButton->setMaximumSize(QSize(buttonSize, buttonSize));
    ui->cam2RemoveMotionRegionsToolButton->setMaximumSize(QSize(buttonSize, buttonSize));
    ui->cam2ImageToolButton->setMaximumSize(QSize(buttonSize, buttonSize));
    ui->cam2RecordToolButton->setMaximumSize(QSize(buttonSize, buttonSize));
    ui->cam2ExpandToolButton->setMaximumSize(QSize(buttonSize, buttonSize));
    ui->cam2StorageToolButton->setMaximumSize(QSize(buttonSize, buttonSize));

    ui->cam3SettingsToolButton->setMaximumSize(QSize(buttonSize, buttonSize));
    ui->cam3ConnectToolButton->setMaximumSize(QSize(buttonSize, buttonSize));
    ui->cam3MotionRegionsToolButton->setMaximumSize(QSize(buttonSize, buttonSize));
    ui->cam3RemoveMotionRegionsToolButton->setMaximumSize(QSize(buttonSize, buttonSize));
    ui->cam3ImageToolButton->setMaximumSize(QSize(buttonSize, buttonSize));
    ui->cam3RecordToolButton->setMaximumSize(QSize(buttonSize, buttonSize));
    ui->cam3ExpandToolButton->setMaximumSize(QSize(buttonSize, buttonSize));
    ui->cam3StorageToolButton->setMaximumSize(QSize(buttonSize, buttonSize));
}

void IpFreelyMainWindow::ConnectButtons()
{
    connect(ui->cam1SettingsToolButton,
            &QToolButton::clicked,
            this,
            &IpFreelyMainWindow::on_settings1ToolButton_clicked);

    connect(ui->cam2SettingsToolButton,
            &QToolButton::clicked,
            this,
            &IpFreelyMainWindow::on_settings2ToolButton_clicked);

    connect(ui->cam3SettingsToolButton,
            &QToolButton::clicked,
            this,
            &IpFreelyMainWindow::on_settings3ToolButton_clicked);

    connect(ui->cam4SettingsToolButton,
            &QToolButton::clicked,
            this,
            &IpFreelyMainWindow::on_settings4ToolButton_clicked);

    connect(ui->cam1ConnectToolButton,
            &QToolButton::clicked,
            this,
            &IpFreelyMainWindow::on_connect1ToolButton_clicked);

    connect(ui->cam2ConnectToolButton,
            &QToolButton::clicked,
            this,
            &IpFreelyMainWindow::on_connect2ToolButton_clicked);

    connect(ui->cam3ConnectToolButton,
            &QToolButton::clicked,
            this,
            &IpFreelyMainWindow::on_connect3ToolButton_clicked);

    connect(ui->cam4ConnectToolButton,
            &QToolButton::clicked,
            this,
            &IpFreelyMainWindow::on_connect4ToolButton_clicked);

    connect(ui->cam1ImageToolButton,
            &QToolButton::clicked,
            this,
            &IpFreelyMainWindow::on_snapshot1ToolButton_clicked);

    connect(ui->cam2ImageToolButton,
            &QToolButton::clicked,
            this,
            &IpFreelyMainWindow::on_snapshot2ToolButton_clicked);

    connect(ui->cam3ImageToolButton,
            &QToolButton::clicked,
            this,
            &IpFreelyMainWindow::on_snapshot3ToolButton_clicked);

    connect(ui->cam4ImageToolButton,
            &QToolButton::clicked,
            this,
            &IpFreelyMainWindow::on_snapshot4ToolButton_clicked);

    connect(ui->cam1RecordToolButton,
            &QToolButton::clicked,
            this,
            &IpFreelyMainWindow::on_record1ToolButton_clicked);

    connect(ui->cam2RecordToolButton,
            &QToolButton::clicked,
            this,
            &IpFreelyMainWindow::on_record2ToolButton_clicked);

    connect(ui->cam3RecordToolButton,
            &QToolButton::clicked,
            this,
            &IpFreelyMainWindow::on_record3ToolButton_clicked);

    connect(ui->cam4RecordToolButton,
            &QToolButton::clicked,
            this,
            &IpFreelyMainWindow::on_record4ToolButton_clicked);

    connect(ui->cam1ExpandToolButton,
            &QToolButton::clicked,
            this,
            &IpFreelyMainWindow::on_expand1ToolButton_clicked);

    connect(ui->cam2ExpandToolButton,
            &QToolButton::clicked,
            this,
            &IpFreelyMainWindow::on_expand2ToolButton_clicked);

    connect(ui->cam3ExpandToolButton,
            &QToolButton::clicked,
            this,
            &IpFreelyMainWindow::on_expand3ToolButton_clicked);

    connect(ui->cam4ExpandToolButton,
            &QToolButton::clicked,
            this,
            &IpFreelyMainWindow::on_expand4ToolButton_clicked);

    connect(ui->cam1StorageToolButton,
            &QToolButton::clicked,
            this,
            &IpFreelyMainWindow::on_storage1ToolButton_clicked);

    connect(ui->cam2StorageToolButton,
            &QToolButton::clicked,
            this,
            &IpFreelyMainWindow::on_storage2ToolButton_clicked);

    connect(ui->cam3StorageToolButton,
            &QToolButton::clicked,
            this,
            &IpFreelyMainWindow::on_storage3ToolButton_clicked);

    connect(ui->cam4StorageToolButton,
            &QToolButton::clicked,
            this,
            &IpFreelyMainWindow::on_storage4ToolButton_clicked);

    connect(ui->cam1MotionRegionsToolButton,
            &QToolButton::toggled,
            this,
            &IpFreelyMainWindow::on_motionDetectorRegions1ToolButton_toggled);

    connect(ui->cam2MotionRegionsToolButton,
            &QToolButton::toggled,
            this,
            &IpFreelyMainWindow::on_motionDetectorRegions2ToolButton_toggled);

    connect(ui->cam3MotionRegionsToolButton,
            &QToolButton::toggled,
            this,
            &IpFreelyMainWindow::on_motionDetectorRegions3ToolButton_toggled);

    connect(ui->cam4MotionRegionsToolButton,
            &QToolButton::toggled,
            this,
            &IpFreelyMainWindow::on_motionDetectorRegions4ToolButton_toggled);

    connect(ui->cam1RemoveMotionRegionsToolButton,
            &QToolButton::clicked,
            this,
            &IpFreelyMainWindow::on_removeMotionRegions1ToolButton_clicked);

    connect(ui->cam2RemoveMotionRegionsToolButton,
            &QToolButton::clicked,
            this,
            &IpFreelyMainWindow::on_removeMotionRegions2ToolButton_clicked);

    connect(ui->cam3RemoveMotionRegionsToolButton,
            &QToolButton::clicked,
            this,
            &IpFreelyMainWindow::on_removeMotionRegions3ToolButton_clicked);

    connect(ui->cam4RemoveMotionRegionsToolButton,
            &QToolButton::clicked,
            this,
            &IpFreelyMainWindow::on_removeMotionRegions4ToolButton_clicked);
}

void IpFreelyMainWindow::CheckStartupConnections()
{
    ui->cam1ConnectToolButton->setEnabled(m_camDb.DoesCameraExist(ipfreely::eCamId::cam1));

    if (ui->cam1ConnectToolButton->isEnabled() && m_prefs.ConnectToCamerasOnStartup())
    {
        on_connect1ToolButton_clicked();
    }

    ui->cam2ConnectToolButton->setEnabled(m_camDb.DoesCameraExist(ipfreely::eCamId::cam2));

    if (ui->cam2ConnectToolButton->isEnabled() && m_prefs.ConnectToCamerasOnStartup())
    {
        on_connect2ToolButton_clicked();
    }

    ui->cam3ConnectToolButton->setEnabled(m_camDb.DoesCameraExist(ipfreely::eCamId::cam3));

    if (ui->cam3ConnectToolButton->isEnabled() && m_prefs.ConnectToCamerasOnStartup())
    {
        on_connect3ToolButton_clicked();
    }

    ui->cam4ConnectToolButton->setEnabled(m_camDb.DoesCameraExist(ipfreely::eCamId::cam4));

    if (ui->cam4ConnectToolButton->isEnabled() && m_prefs.ConnectToCamerasOnStartup())
    {
        on_connect4ToolButton_clicked();
    }
}

void IpFreelyMainWindow::SetupCameraInDb(ipfreely::eCamId const camId, QToolButton* connectBtn)
{
    ipfreely::IpCamera camera;

    if (!m_camDb.FindCamera(camId, camera))
    {
        DEBUG_MESSAGE_EX_ERROR("Failed to find camera, ID: " << static_cast<int>(camId));
        camera.camId = camId;
    }

    IpFreelyCameraSetupDialog camSettingsDlg(camera, this);
    camSettingsDlg.setModal(true);

    if (camSettingsDlg.exec() == QDialog::Rejected)
    {
        return;
    }

    if (camera.IsValid())
    {
        m_camDb.UpdateCamera(camera);
    }
    else
    {
        m_camDb.RemoveCamera(camId);
    }

    m_camDb.Save();
    connectBtn->setEnabled(camera.IsValid());
}

void IpFreelyMainWindow::ConnectionHandler(ipfreely::IpCamera const& camera,
                                           QToolButton* connectBtn, QToolButton* motionRegionsBtn,
                                           QToolButton* removeRegionsBtn, QToolButton* recordBtn,
                                           QToolButton* snapshotBtn, QToolButton* expandBtn,
                                           QToolButton* storageBtn)
{
    if (m_updateFeedsTimer->isActive())
    {
        m_updateFeedsTimer->stop();
    }

    if (m_streamProcessors.count(camera.camId) > 0)
    {
        if (m_videoForm->isVisible() && (m_videoFormId == camera.camId))
        {
            m_videoForm->close();
            m_videoFormId = ipfreely::eCamId::noCam;
        }

        m_streamProcessors.erase(camera.camId);
        m_camFeeds.erase(camera.camId);
        m_camMotionRegions.erase(camera.camId);

        switch (camera.camId)
        {
        case ipfreely::eCamId::cam1:
            ClearLayout(ui->cam1Widget->layout(), true);
            ui->camFeed1GroupBox->setTitle(tr("Camera 1"));
            ui->camFeed1GroupBox->setToolTip(tr("Not connected"));
            break;
        case ipfreely::eCamId::cam2:
            ClearLayout(ui->cam2Widget->layout(), true);
            ui->camFeed2GroupBox->setTitle(tr("Camera 2"));
            ui->camFeed2GroupBox->setToolTip(tr("Not connected"));
            break;
        case ipfreely::eCamId::cam3:
            ClearLayout(ui->cam3Widget->layout(), true);
            ui->camFeed3GroupBox->setTitle(tr("Camera 3"));
            ui->camFeed3GroupBox->setToolTip(tr("Not connected"));
            break;
        case ipfreely::eCamId::cam4:
            ClearLayout(ui->cam4Widget->layout(), true);
            ui->camFeed4GroupBox->setTitle(tr("Camera 4"));
            ui->camFeed4GroupBox->setToolTip(tr("Not connected"));
            break;
        case ipfreely::eCamId::noCam:
            // Do nothing.
            break;
        }

        connectBtn->setIcon(QIcon(":/icons/icons/WallCam_Connect_48.png"));
        connectBtn->setToolTip("Connect to camera stream.");

        recordBtn->setEnabled(false);
        recordBtn->setIcon(QIcon(":/icons/icons/Record-48.png"));
        recordBtn->setToolTip("Record from camera stream.");

        snapshotBtn->setEnabled(false);
        expandBtn->setEnabled(false);
        storageBtn->setEnabled(false);
        motionRegionsBtn->setEnabled(false);
        motionRegionsBtn->setChecked(false);
        removeRegionsBtn->setVisible(false);

        m_camMotionRegions.erase(camera.camId);
        m_motionAreaSetupEnabled.erase(camera.camId);

        if (--m_numConnections > 0)
        {
            m_updateFeedsTimer->start(DEFAULT_UPDATE_PERIOD_MS);
        }
    }
    else
    {
        std::string camName;
        int         cameraId = 0;

        switch (camera.camId)
        {
        case ipfreely::eCamId::cam1:
            camName  = "Camera1";
            cameraId = 1;
            break;
        case ipfreely::eCamId::cam2:
            camName  = "Camera2";
            cameraId = 2;
            break;
        case ipfreely::eCamId::cam3:
            camName  = "Camera3";
            cameraId = 3;
            break;
        case ipfreely::eCamId::cam4:
            camName  = "Camera4";
            cameraId = 4;
            break;
        case ipfreely::eCamId::noCam:
            // Do nothing.
            break;
        }

        try
        {
            bfs::path p(m_prefs.SaveFolderPath());
            p = bfs::system_complete(p);

            auto schedule = m_prefs.RecordingSchedule();

            if (!camera.enableScheduledRecording)
            {
                schedule.clear();
            }

            auto motionSchedule = m_prefs.MotionTrackingSchedule();

            if (!camera.enabledMotionRecording)
            {
                motionSchedule.clear();
            }

            m_streamProcessors[camera.camId] =
                std::make_shared<ipfreely::IpFreelyStreamProcessor>(camName,
                                                                    camera,
                                                                    p.string(),
                                                                    m_prefs.FileDurationInSecs(),
                                                                    schedule,
                                                                    motionSchedule);
        }
        catch (std::exception& e)
        {
            DEBUG_MESSAGE_EX_ERROR("Stream Error, camera: " << camName
                                                            << ", error message: " << e.what());
            QMessageBox::critical(this,
                                  "Stream Error",
                                  QString::fromLocal8Bit(e.what()),
                                  QMessageBox::Ok,
                                  QMessageBox::Ok);
            return;
        }

        auto feed = new IpFreelyVideoFrame(cameraId,
                                           std::bind(&IpFreelyMainWindow::VideoFrameAreaSelection,
                                                     this,
                                                     std::placeholders::_1,
                                                     std::placeholders::_2),
                                           this);
        ;
        QString hint = QString::fromStdString(camera.description);

        switch (camera.camId)
        {
        case ipfreely::eCamId::cam1:
            ui->cam1Widget->layout()->addWidget(feed);
            ui->camFeed1GroupBox->setToolTip(hint);
            break;
        case ipfreely::eCamId::cam2:
            ui->cam2Widget->layout()->addWidget(feed);
            ui->camFeed2GroupBox->setToolTip(hint);
            break;
        case ipfreely::eCamId::cam3:
            ui->cam3Widget->layout()->addWidget(feed);
            ui->camFeed3GroupBox->setToolTip(hint);
            break;
        case ipfreely::eCamId::cam4:
            ui->cam4Widget->layout()->addWidget(feed);
            ui->camFeed4GroupBox->setToolTip(hint);
            break;
        case ipfreely::eCamId::noCam:
            // Do nothing.
            break;
        }

        m_camFeeds[camera.camId] = feed;

        recordBtn->setEnabled(!camera.enableScheduledRecording);
        recordBtn->setIcon(QIcon(":/icons/icons/Record-48.png"));
        recordBtn->setToolTip("Record from camera stream.");

        snapshotBtn->setEnabled(true);
        expandBtn->setEnabled(true);
        storageBtn->setEnabled(!camera.storageHttpUrl.empty());
        motionRegionsBtn->setEnabled(true);

        connectBtn->setIcon(QIcon(":/icons/icons/WallCam_Disconnect_48.png"));
        connectBtn->setToolTip("Disconnect from camera stream.");

        ++m_numConnections;

        m_updateFeedsTimer->start(DEFAULT_UPDATE_PERIOD_MS);
    }
}

void IpFreelyMainWindow::RecordActionHandler(ipfreely::eCamId const camId, QToolButton* recordBtn)
{
    auto streamProcIter = m_streamProcessors.find(camId);

    if (streamProcIter == m_streamProcessors.end())
    {
        DEBUG_MESSAGE_EX_ERROR("Failed to find stream processor, ID: " << static_cast<int>(camId));
        return;
    }

    if (streamProcIter->second->VideoWritingEnabled())
    {
        streamProcIter->second->StopVideoWriting();

        recordBtn->setIcon(QIcon(":/icons/icons/Record-48.png"));
        recordBtn->setToolTip("Record from camera stream.");
    }
    else
    {
        streamProcIter->second->StartVideoWriting();

        recordBtn->setIcon(QIcon(":/icons/icons/Stop-48.png"));
        recordBtn->setToolTip("Stop recording from camera stream.");
    }
}

void IpFreelyMainWindow::UpdateCamFeedFrame(ipfreely::eCamId const camId, QImage const& videoFrame,
                                            QRect const& motionBoundingRect,
                                            bool const   streamProcIsWriting)
{
    auto camFeedIter = m_camFeeds.find(camId);

    if (camFeedIter == m_camFeeds.end())
    {
        DEBUG_MESSAGE_EX_ERROR("Failed to find camera feed, ID: " << static_cast<int>(camId));
        return;
    }

    QImage displayFrame;
    double scalar = 1.0;

    if ((camFeedIter->second->width() < videoFrame.width()) ||
        (camFeedIter->second->height() < videoFrame.height()))
    {
        double frameAspectRatio =
            static_cast<double>(videoFrame.width()) / static_cast<double>(videoFrame.height());
        double targetAspectRatio = static_cast<double>(camFeedIter->second->width()) /
                                   static_cast<double>(camFeedIter->second->height());
        int newWidth  = 0;
        int newHeight = 0;

        if (targetAspectRatio < frameAspectRatio)
        {
            newWidth  = camFeedIter->second->width();
            newHeight = static_cast<int>(static_cast<double>(newWidth) / frameAspectRatio);
        }
        else
        {
            newHeight = camFeedIter->second->height();
            newWidth  = static_cast<int>(static_cast<double>(newHeight) * frameAspectRatio);
        }

        scalar = static_cast<double>(newWidth) / static_cast<double>(videoFrame.width());

        displayFrame =
            videoFrame.scaled(newWidth, newHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    else
    {
        displayFrame = videoFrame;
    }

    auto motionAreasEnabled = m_motionAreaSetupEnabled[camId];

    if (!motionBoundingRect.isNull() || streamProcIsWriting || motionAreasEnabled)
    {
        QPainter p(&displayFrame);
        QRect    rect                   = motionBoundingRect;
        bool     intersectsMotionRegion = false;

        if (!motionBoundingRect.isNull())
        {
            rect.setTop(static_cast<int>(static_cast<double>(motionBoundingRect.top()) * scalar));
            rect.setLeft(static_cast<int>(static_cast<double>(motionBoundingRect.left()) * scalar));
            rect.setRight(
                static_cast<int>(static_cast<double>(motionBoundingRect.right()) * scalar));
            rect.setBottom(
                static_cast<int>(static_cast<double>(motionBoundingRect.bottom()) * scalar));
        }

        if (motionAreasEnabled)
        {
            auto motionRectAreas = m_camMotionRegions[camId];
            auto pen             = QPen(Qt::cyan);
            pen.setWidth(2);
            p.setPen(pen);
            p.setBackground(QBrush(Qt::NoBrush));
            p.setBackgroundMode(Qt::TransparentMode);
            p.setBrush(QBrush(Qt::NoBrush));

            for (auto const& motionRegion : motionRectAreas)
            {
                auto r = ipfreely::CreateQRectFromVideoFrameDims(
                    displayFrame.width(), displayFrame.height(), motionRegion);

                if (rect.intersects(r))
                {
                    intersectsMotionRegion = true;
                }

                p.drawRect(r);
            }
        }

        if (!rect.isNull())
        {
            auto pen = QPen(intersectsMotionRegion ? Qt::red : Qt::green);
            pen.setWidth(2);
            p.setPen(pen);
            p.setBackground(QBrush(Qt::NoBrush));
            p.setBackgroundMode(Qt::TransparentMode);
            p.setBrush(QBrush(Qt::NoBrush));
            p.drawRect(rect);
        }

        if (streamProcIsWriting)
        {
            p.setPen(QPen(Qt::red));
            p.setBackground(QBrush(Qt::white, Qt::SolidPattern));
            p.setBackgroundMode(Qt::OpaqueMode);
            p.setFont(QFont("Segoe UI", 16, QFont::Bold));
            auto posRec = displayFrame.rect();
            posRec.setTop(posRec.top() + 16);
            p.drawText(posRec, Qt::AlignHCenter | Qt::AlignTop, tr("Recording").toLocal8Bit());
        }
    }

    camFeedIter->second->SetVideoFrame(displayFrame);
}

void IpFreelyMainWindow::SaveImageSnapshot(ipfreely::eCamId const camId)
{
    auto streamProcIter = m_streamProcessors.find(camId);

    if (streamProcIter == m_streamProcessors.end())
    {
        DEBUG_MESSAGE_EX_ERROR("Failed to find stream processor, ID: " << static_cast<int>(camId));
        return;
    }

    time_t timestamp = time(0);
    auto   localTime = std::localtime(&timestamp);
    char   folderName[9];
    std::strftime(folderName, sizeof(folderName), "%Y%m%d", localTime);

    bfs::path p(m_prefs.SaveFolderPath());
    p /= folderName;
    p = bfs::system_complete(p);

    if (!bfs::exists(p))
    {
        if (!bfs::create_directories(p))
        {
            std::ostringstream oss;
            oss << "Failed to create directories: " << p.string();
            DEBUG_MESSAGE_EX_ERROR(oss.str());
            QMessageBox::critical(this,
                                  "Filesystem Error",
                                  QString::fromStdString(oss.str()),
                                  QMessageBox::Ok,
                                  QMessageBox::Ok);
            return;
        }
    }

    std::string camName;

    switch (camId)
    {
    case ipfreely::eCamId::cam1:
        camName = "Camera1";
        break;
    case ipfreely::eCamId::cam2:
        camName = "Camera2";
        break;
    case ipfreely::eCamId::cam3:
        camName = "Camera3";
        break;
    case ipfreely::eCamId::cam4:
        camName = "Camera4";
        break;
    case ipfreely::eCamId::noCam:
        // Do nothing.
        break;
    }

    std::ostringstream fileOss;
    fileOss << camName << "_" << timestamp << ".png";
    p /= fileOss.str();
    p = bfs::system_complete(p);

    DEBUG_MESSAGE_EX_INFO("Creating new output image file: " << p.string());

    auto videoFrame = streamProcIter->second->CurrentVideoFrame();

    if (!videoFrame.save(QString::fromStdString(p.string())))
    {
        std::ostringstream oss;
        oss << "Failed to save snapshot image for camera: " << camName << ", to: " << p.string();
        DEBUG_MESSAGE_EX_ERROR(oss.str());
        QMessageBox::critical(this,
                              "Snapshot Error",
                              QString::fromStdString(oss.str()),
                              QMessageBox::Ok,
                              QMessageBox::Ok);
    }
}

void IpFreelyMainWindow::SetFpsInTitle(ipfreely::eCamId const camId, double fps, double originalFps)
{
    switch (camId)
    {
    case ipfreely::eCamId::cam1:
        ui->camFeed1GroupBox->setTitle(tr("Camera 1: ") + QString::number(fps) +
                                       tr(" Recording FPS, ") + QString::number(originalFps) +
                                       tr(" Stream FPS"));
        break;
    case ipfreely::eCamId::cam2:
        ui->camFeed2GroupBox->setTitle(tr("Camera 2: ") + QString::number(fps) +
                                       tr(" Recording FPS, ") + QString::number(originalFps) +
                                       tr(" Stream FPS"));
        break;
    case ipfreely::eCamId::cam3:
        ui->camFeed3GroupBox->setTitle(tr("Camera 3: ") + QString::number(fps) +
                                       tr(" Recording FPS, ") + QString::number(originalFps) +
                                       tr(" Stream FPS"));
        break;
    case ipfreely::eCamId::cam4:
        ui->camFeed4GroupBox->setTitle(tr("Camera 4: ") + QString::number(fps) +
                                       tr(" Recording FPS, ") + QString::number(originalFps) +
                                       tr(" Stream FPS"));
        break;
    case ipfreely::eCamId::noCam:
        // Do nothing.
        break;
    }
}

void IpFreelyMainWindow::ShowExpandedVideoForm(ipfreely::eCamId const camId)
{
    if (m_videoForm->isVisible())
    {
        m_videoForm->close();
    }

    switch (camId)
    {
    case ipfreely::eCamId::cam1:
        m_videoForm->SetTitle(tr("Camera 1"));
        break;
    case ipfreely::eCamId::cam2:
        m_videoForm->SetTitle(tr("Camera 2"));
        break;
    case ipfreely::eCamId::cam3:
        m_videoForm->SetTitle(tr("Camera 3"));
        break;
    case ipfreely::eCamId::cam4:
        m_videoForm->SetTitle(tr("Camera 4"));
        break;
    case ipfreely::eCamId::noCam:
        // Do nothing.
        break;
    }

    m_videoFormId = camId;
    m_videoForm->show();
}

void IpFreelyMainWindow::ViewStorage(ipfreely::IpCamera const& camera)
{
    IpFreelySdCardViewerDialog sdCardDlg(camera, this);
    sdCardDlg.setModal(true);
    sdCardDlg.exec();
}

void IpFreelyMainWindow::VideoFrameAreaSelection(int const     cameraId,
                                                 QRectF const& percentageSelection)
{
    ipfreely::eCamId camId;

    switch (cameraId)
    {
    case 1:
        camId = ipfreely::eCamId::cam1;
        break;
    case 2:
        camId = ipfreely::eCamId::cam2;
        break;
    case 3:
        camId = ipfreely::eCamId::cam3;
        break;
    case 4:
        camId = ipfreely::eCamId::cam4;
        break;
    default:
        DEBUG_MESSAGE_EX_ERROR("Invalid camera ID value: " << cameraId);
        return;
    }

    ipfreely::IpCamera::point_t  leftTop(percentageSelection.left(), percentageSelection.top());
    ipfreely::IpCamera::point_t  widthHeight(percentageSelection.width(),
                                            percentageSelection.height());
    ipfreely::IpCamera::region_t region(leftTop, widthHeight);
    m_camMotionRegions[camId].emplace_back(region);

    ipfreely::IpCamera camera;

    if (m_camDb.FindCamera(camId, camera))
    {
        camera.motionRegions = m_camMotionRegions[camId];
        m_camDb.UpdateCamera(camera);
        m_camDb.Save();

        ReconnectCamera(camId);
    }
}

void IpFreelyMainWindow::EnableMotionRegionsSetup(ipfreely::eCamId const camId, bool const enable,
                                                  QToolButton* removeRegionsBtn,
                                                  QToolButton* setMotionRegionsBtn)
{
    removeRegionsBtn->setVisible(enable);

    auto camFeedIter = m_camFeeds.find(camId);

    if (camFeedIter == m_camFeeds.end())
    {
        DEBUG_MESSAGE_EX_ERROR("Failed to find camera feed, ID: " << static_cast<int>(camId));
        return;
    }

    camFeedIter->second->SetEnableSelection(enable);

    if (enable)
    {
        ipfreely::IpCamera camera;

        if (m_camDb.FindCamera(camId, camera))
        {
            m_camMotionRegions[camId]       = camera.motionRegions;
            m_motionAreaSetupEnabled[camId] = true;
        }

        bool blockState = setMotionRegionsBtn->blockSignals(true);
        setMotionRegionsBtn->setChecked(true);
        setMotionRegionsBtn->blockSignals(blockState);
    }
    else
    {
        m_camMotionRegions.erase(camId);
        m_motionAreaSetupEnabled.erase(camId);
    }
}

void IpFreelyMainWindow::RemoveMotionRegions(ipfreely::eCamId const camId)
{
    ipfreely::IpCamera camera;

    if (!m_camDb.FindCamera(camId, camera))
    {
        DEBUG_MESSAGE_EX_ERROR("Failed to find camera, ID: " << static_cast<int>(camId));
        return;
    }

    if (QMessageBox::No ==
        QMessageBox::question(
            this,
            tr("Question"),
            tr("Do you want to remove the motion detection regions from this camera?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No))
    {
        return;
    }

    m_camMotionRegions[camId].clear();
    camera.motionRegions = m_camMotionRegions[camId];
    m_camDb.UpdateCamera(camera);
    m_camDb.Save();

    ReconnectCamera(camId);
}

void IpFreelyMainWindow::ReconnectCamera(ipfreely::eCamId const camId)
{
    switch (camId)
    {
    case ipfreely::eCamId::cam1:
        on_connect1ToolButton_clicked();
        on_connect1ToolButton_clicked();
        EnableMotionRegionsSetup(
            camId, true, ui->cam1RemoveMotionRegionsToolButton, ui->cam1MotionRegionsToolButton);
        break;
    case ipfreely::eCamId::cam2:
        on_connect2ToolButton_clicked();
        on_connect2ToolButton_clicked();
        EnableMotionRegionsSetup(
            camId, true, ui->cam2RemoveMotionRegionsToolButton, ui->cam2MotionRegionsToolButton);
        break;
    case ipfreely::eCamId::cam3:
        on_connect3ToolButton_clicked();
        on_connect3ToolButton_clicked();
        EnableMotionRegionsSetup(
            camId, true, ui->cam3RemoveMotionRegionsToolButton, ui->cam3MotionRegionsToolButton);
        break;
    case ipfreely::eCamId::cam4:
        on_connect4ToolButton_clicked();
        on_connect4ToolButton_clicked();
        EnableMotionRegionsSetup(
            camId, true, ui->cam4RemoveMotionRegionsToolButton, ui->cam4MotionRegionsToolButton);
        break;
    case ipfreely::eCamId::noCam:
        // Do nothing
        break;
    }
}
