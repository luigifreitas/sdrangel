///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016 Edouard Griffiths, F4EXB                                   //
//                                                                               //
// This program is free software; you can redistribute it and/or modify          //
// it under the terms of the GNU General Public License as published by          //
// the Free Software Foundation as version 3 of the License, or                  //
//                                                                               //
// This program is distributed in the hope that it will be useful,               //
// but WITHOUT ANY WARRANTY; without even the implied warranty of                //
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                  //
// GNU General Public License V3 for more details.                               //
//                                                                               //
// You should have received a copy of the GNU General Public License             //
// along with this program. If not, see <http://www.gnu.org/licenses/>.          //
///////////////////////////////////////////////////////////////////////////////////

#include <QDebug>

#include <QTime>
#include <QDateTime>
#include <QString>
#include <QFileDialog>
#include <QMessageBox>

#include "ui_filesinkgui.h"
#include "plugin/pluginapi.h"
#include "gui/colormapper.h"
#include "gui/glspectrum.h"
#include "dsp/dspengine.h"
#include "dsp/dspcommands.h"

#include "mainwindow.h"

#include "device/devicesinkapi.h"
#include "filesinkgui.h"

FileSinkGui::FileSinkGui(DeviceSinkAPI *deviceAPI, QWidget* parent) :
	QWidget(parent),
	ui(new Ui::FileSinkGui),
	m_deviceAPI(deviceAPI),
	m_settings(),
	m_sampleSink(NULL),
	m_generation(false),
	m_fileName("..."),
	m_sampleRate(0),
	m_centerFrequency(0),
	m_startingTimeStamp(0),
	m_samplesCount(0),
	m_tickCount(0),
	m_enableNavTime(false),
	m_lastEngineState((DSPDeviceSinkEngine::State)-1)
{
	ui->setupUi(this);
	ui->centerFrequency->setColorMapper(ColorMapper(ColorMapper::ReverseGold));
	ui->centerFrequency->setValueRange(7, 0, pow(10,7));
	ui->fileNameText->setText(m_fileName);

	ui->samplerate->clear();
	for (int i = 0; i < FileSinkSampleRates::getNbRates(); i++)
	{
		ui->samplerate->addItem(QString::number(FileSinkSampleRates::getRate(i)));
	}

	connect(&(m_deviceAPI->getMainWindow()->getMasterTimer()), SIGNAL(timeout()), this, SLOT(tick()));
	connect(&m_statusTimer, SIGNAL(timeout()), this, SLOT(updateStatus()));
	m_statusTimer.start(500);

	displaySettings();

	m_sampleSink = new FileSinkOutput(m_deviceAPI->getMainWindow()->getMasterTimer());
	connect(m_sampleSink->getOutputMessageQueueToGUI(), SIGNAL(messageEnqueued()), this, SLOT(handleSinkMessages()));
	m_deviceAPI->setSink(m_sampleSink);

    connect(m_deviceAPI->getDeviceOutputMessageQueue(), SIGNAL(messageEnqueued()), this, SLOT(handleDSPMessages()), Qt::QueuedConnection);
}

FileSinkGui::~FileSinkGui()
{
	delete ui;
}

void FileSinkGui::destroy()
{
	delete this;
}

void FileSinkGui::setName(const QString& name)
{
	setObjectName(name);
}

QString FileSinkGui::getName() const
{
	return objectName();
}

void FileSinkGui::resetToDefaults()
{
	m_settings.resetToDefaults();
	displaySettings();
	sendSettings();
}

qint64 FileSinkGui::getCenterFrequency() const
{
	return m_centerFrequency;
}

void FileSinkGui::setCenterFrequency(qint64 centerFrequency)
{
	m_centerFrequency = centerFrequency;
	displaySettings();
	sendSettings();
}

QByteArray FileSinkGui::serialize() const
{
	return m_settings.serialize();
}

bool FileSinkGui::deserialize(const QByteArray& data)
{
	if(m_settings.deserialize(data)) {
		displaySettings();
		sendSettings();
		return true;
	} else {
		resetToDefaults();
		return false;
	}
}

void FileSinkGui::handleDSPMessages()
{
    Message* message;

    while ((message = m_deviceAPI->getDeviceOutputMessageQueue()->pop()) != 0)
    {
        qDebug("FileSinkGui::handleDSPMessages: message: %s", message->getIdentifier());

        if (DSPSignalNotification::match(*message))
        {
            DSPSignalNotification* notif = (DSPSignalNotification*) message;
            m_deviceSampleRate = notif->getSampleRate();
            m_deviceCenterFrequency = notif->getCenterFrequency();
            qDebug("FileSinkGui::handleDSPMessages: SampleRate:%d, CenterFrequency:%llu", notif->getSampleRate(), notif->getCenterFrequency());
            updateSampleRateAndFrequency();

            delete message;
        }
    }
}

bool FileSinkGui::handleMessage(const Message& message)
{
	if (FileSinkOutput::MsgReportFileSinkGeneration::match(message))
	{
		m_generation = ((FileSinkOutput::MsgReportFileSinkGeneration&)message).getAcquisition();
		updateWithGeneration();
		return true;
	}
	else if (FileSinkOutput::MsgReportFileSinkStreamData::match(message))
	{
		m_sampleRate = ((FileSinkOutput::MsgReportFileSinkStreamData&)message).getSampleRate();
		m_centerFrequency = ((FileSinkOutput::MsgReportFileSinkStreamData&)message).getCenterFrequency();
		m_startingTimeStamp = ((FileSinkOutput::MsgReportFileSinkStreamData&)message).getStartingTimeStamp();
		updateWithStreamData();
		return true;
	}
	else if (FileSinkOutput::MsgReportFileSinkStreamTiming::match(message))
	{
		m_samplesCount = ((FileSinkOutput::MsgReportFileSinkStreamTiming&)message).getSamplesCount();
		updateWithStreamTime();
		return true;
	}
	else
	{
		return false;
	}
}

void FileSinkGui::handleSinkMessages()
{
	Message* message;

	while ((message = m_sampleSink->getOutputMessageQueueToGUI()->pop()) != 0)
	{
		//qDebug("FileSourceGui::handleSourceMessages: message: %s", message->getIdentifier());

		if (handleMessage(*message))
		{
			delete message;
		}
	}
}

void FileSinkGui::updateSampleRateAndFrequency()
{
    m_deviceAPI->getSpectrum()->setSampleRate(m_deviceSampleRate);
    m_deviceAPI->getSpectrum()->setCenterFrequency(m_deviceCenterFrequency);
    ui->deviceRateText->setText(tr("%1k").arg((float)m_deviceSampleRate / 1000));
}

void FileSinkGui::displaySettings()
{
}

void FileSinkGui::sendSettings()
{
}

void FileSinkGui::on_startStop_toggled(bool checked)
{
    if (checked)
    {
        if (m_deviceAPI->initGeneration())
        {
            m_deviceAPI->startGeneration();
            DSPEngine::instance()->startAudio();
        }
    }
    else
    {
        m_deviceAPI->stopGeneration();
        DSPEngine::instance()->stopAudio();
    }
}

void FileSinkGui::updateStatus()
{
    int state = m_deviceAPI->state();

    if(m_lastEngineState != state)
    {
        switch(state)
        {
            case DSPDeviceSourceEngine::StNotStarted:
                ui->startStop->setStyleSheet("QToolButton { background:rgb(79,79,79); }");
                break;
            case DSPDeviceSourceEngine::StIdle:
                ui->startStop->setStyleSheet("QToolButton { background-color : blue; }");
                break;
            case DSPDeviceSourceEngine::StRunning:
                ui->startStop->setStyleSheet("QToolButton { background-color : green; }");
                break;
            case DSPDeviceSourceEngine::StError:
                ui->startStop->setStyleSheet("QToolButton { background-color : red; }");
                QMessageBox::information(this, tr("Message"), m_deviceAPI->errorMessage());
                break;
            default:
                break;
        }

        m_lastEngineState = state;
    }
}

void FileSinkGui::on_play_toggled(bool checked)
{
	FileSinkOutput::MsgConfigureFileSinkWork* message = FileSinkOutput::MsgConfigureFileSinkWork::create(checked);
	m_sampleSink->getInputMessageQueue()->push(message);
}

void FileSinkGui::on_showFileDialog_clicked(bool checked)
{
	QString fileName = QFileDialog::getOpenFileName(this,
	    tr("Save I/Q record file"), ".", tr("SDR I/Q Files (*.sdriq)"));

	if (fileName != "")
	{
		m_fileName = fileName;
		ui->fileNameText->setText(m_fileName);
		configureFileName();
	}
}

void FileSinkGui::configureFileName()
{
	qDebug() << "FileSinkGui::configureFileName: " << m_fileName.toStdString().c_str();
	FileSinkOutput::MsgConfigureFileSinkName* message = FileSinkOutput::MsgConfigureFileSinkName::create(m_fileName);
	m_sampleSink->getInputMessageQueue()->push(message);
}

void FileSinkGui::updateWithGeneration()
{
	ui->play->setEnabled(m_generation);
	ui->play->setChecked(m_generation);
	ui->showFileDialog->setEnabled(!m_generation);
}

void FileSinkGui::updateWithStreamData()
{
	ui->centerFrequency->setValue(m_centerFrequency/1000);
	ui->sampleRateText->setText(tr("%1k").arg((float)m_sampleRate / 1000));
	ui->play->setEnabled(m_generation);
}

void FileSinkGui::updateWithStreamTime()
{
	int t_sec = 0;
	int t_msec = 0;

	if (m_sampleRate > 0){
		t_msec = ((m_samplesCount * 1000) / m_sampleRate) % 1000;
		t_sec = m_samplesCount / m_sampleRate;
	}

	QTime t(0, 0, 0, 0);
	t = t.addSecs(t_sec);
	t = t.addMSecs(t_msec);
	QString s_timems = t.toString("hh:mm:ss.zzz");
	ui->relTimeText->setText(s_timems);
}

void FileSinkGui::tick()
{
	if ((++m_tickCount & 0xf) == 0) {
		FileSinkOutput::MsgConfigureFileSinkStreamTiming* message = FileSinkOutput::MsgConfigureFileSinkStreamTiming::create();
		m_sampleSink->getInputMessageQueue()->push(message);
	}
}

unsigned int FileSinkSampleRates::m_rates[] = {48};
unsigned int FileSinkSampleRates::m_nb_rates = 1;

unsigned int FileSinkSampleRates::getRate(unsigned int rate_index)
{
	if (rate_index < m_nb_rates)
	{
		return m_rates[rate_index];
	}
	else
	{
		return m_rates[0];
	}
}

unsigned int FileSinkSampleRates::getRateIndex(unsigned int rate)
{
	for (unsigned int i=0; i < m_nb_rates; i++)
	{
		if (rate/1000 == m_rates[i])
		{
			return i;
		}
	}

	return 0;
}

unsigned int FileSinkSampleRates::getNbRates()
{
	return FileSinkSampleRates::m_nb_rates;
}

