/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ScreenCalibrationDialog.h"

#include "gui/core/ScreenCalibrationController.h"
#include "gui/widgets/ScreenAlignmentPreview.h"

#include <QCheckBox>
#include <QColor>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QLabel *description(const QString &text, QWidget *parent)
{
  auto *label = new QLabel(text, parent);
  label->setTextFormat(Qt::PlainText);
  label->setWordWrap(true);
  return label;
}

QLabel *legend(const QString &text, const QColor &color, QWidget *parent)
{
  auto *label = description(text, parent);
  auto palette = label->palette();
  palette.setColor(QPalette::WindowText, color);
  label->setPalette(palette);
  auto font = label->font();
  font.setBold(true);
  label->setFont(font);
  return label;
}
} // namespace

ScreenCalibrationDialog::ScreenCalibrationDialog(
    QWidget *parent, ScreenCalibrationController &controller, const ScreenAlignment &alignment
)
    : QDialog(parent),
      m_controller(&controller),
      m_alignment(alignment)
{
  setObjectName("screenCalibrationDialog");
  setWindowTitle(tr("Align the colored markers"));
  setMinimumSize(480, 360);
  const auto *targetScreen = parent ? parent->screen() : QGuiApplication::primaryScreen();
  const QSize available = targetScreen ? targetScreen->availableGeometry().size() : QSize(900, 800);
  resize(qMin(760, qMax(480, available.width() - 60)), qMin(680, qMax(360, available.height() - 60)));

  auto *layout = new QVBoxLayout(this);
  layout->setSpacing(10);
  auto *scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto *body = new QWidget(scroll);
  auto *bodyLayout = new QVBoxLayout(body);
  bodyLayout->setContentsMargins(0, 0, 6, 0);
  bodyLayout->setSpacing(12);

  auto *instructions = description(
      tr("Look at the markers on the real displays. Drag the blue A markers until they line up across the seam, "
         "then align the orange B markers. Keep the two colors well apart on each display."),
      body
  );
  instructions->setObjectName("calibrationInstructions");
  bodyLayout->addWidget(instructions);
  auto *colors = new QHBoxLayout;
  colors->addWidget(legend(tr("A — blue"), QColor("#1976d2"), body));
  colors->addWidget(legend(tr("B — orange"), QColor("#f2a33a"), body));
  colors->addStretch();
  bodyLayout->addLayout(colors);
  bodyLayout->addWidget(description(
      tr("Use the matching letters as well as the colors. Two matched positions determine the relative edge size "
         "and offset; no ruler or millimeter measurements are needed."),
      body
  ));

  m_preview = new ScreenAlignmentPreview(body);
  m_preview->setObjectName("calibrationPreview");
  m_preview->setMinimumHeight(190);
  m_preview->setFocusPolicy(Qt::NoFocus);
  m_preview->setAttribute(Qt::WA_TransparentForMouseEvents);
  m_preview->hide();
  bodyLayout->addWidget(m_preview, 1);
  m_previewHint = description(tr("The preview will appear when both displays are ready."), body);
  m_previewHint->setObjectName("calibrationPreviewHint");
  bodyLayout->addWidget(m_previewHint);

  m_checkPoint = new QCheckBox(tr("Check the alignment with a third green C marker"), body);
  m_checkPoint->setObjectName("calibrationCheckPoint");
  m_checkPoint->setChecked(controller.checkPointEnabled());
  bodyLayout->addWidget(m_checkPoint);
  bodyLayout->addWidget(description(
      tr("The optional C markers check the remaining error. They do not change the calibration from A and B."), body
  ));
  bodyLayout->addWidget(description(
      tr("Use calibration returns this result to Screen alignment. Save there and in Server Configuration to apply it."
      ),
      body
  ));
  scroll->setWidget(body);
  layout->addWidget(scroll, 1);

  // Readiness, measurements, errors and actions stay visible even when the
  // instructions and schematic need scrolling on a small display.
  m_firstState = description({}, this);
  m_firstState->setObjectName("calibrationFirstState");
  m_secondState = description({}, this);
  m_secondState->setObjectName("calibrationSecondState");
  m_measurement = description({}, this);
  m_measurement->setObjectName("calibrationMeasurement");
  m_measurement->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_status = description({}, this);
  m_status->setObjectName("calibrationStatus");
  layout->addWidget(m_firstState);
  layout->addWidget(m_secondState);
  layout->addWidget(m_measurement);
  layout->addWidget(m_status);

  m_buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
  m_buttons->setObjectName("calibrationButtons");
  m_buttons->button(QDialogButtonBox::Cancel)->setObjectName("cancelCalibration");
  m_use = m_buttons->addButton(tr("Use calibration"), QDialogButtonBox::AcceptRole);
  m_use->setObjectName("useCalibration");
  m_use->setEnabled(false);
  m_retry = m_buttons->addButton(tr("Retry"), QDialogButtonBox::ActionRole);
  m_retry->setObjectName("retryCalibration");
  m_retry->hide();
  layout->addWidget(m_buttons);

  connect(m_buttons, &QDialogButtonBox::accepted, this, &ScreenCalibrationDialog::accept);
  connect(m_buttons, &QDialogButtonBox::rejected, this, &ScreenCalibrationDialog::reject);
  connect(m_retry, &QPushButton::clicked, this, &ScreenCalibrationDialog::startSession);
  connect(m_checkPoint, &QCheckBox::toggled, this, [this](bool enabled) {
    if (m_controller && m_ownsSession && !m_finished)
      m_controller->setCheckPointEnabled(enabled);
  });
  connect(&controller, &ScreenCalibrationController::stateChanged, this, &ScreenCalibrationDialog::updateState);
  connect(&controller, &ScreenCalibrationController::cancelled, this, &ScreenCalibrationDialog::sessionCancelled);
  connect(&controller, &QObject::destroyed, this, [this] {
    sessionCancelled(tr("The calibration connection is no longer available."));
  });
  updateState();
  QTimer::singleShot(0, this, &ScreenCalibrationDialog::startSession);
}

ScreenCalibrationDialog::~ScreenCalibrationDialog()
{
  m_finished = true;
  stopSession();
}

std::optional<ScreenEdgeCalibration> ScreenCalibrationDialog::calibration() const
{
  return m_calibration;
}

void ScreenCalibrationDialog::startSession()
{
  if (m_finished || !m_controller)
    return;
  m_failed = false;
  m_failureReason.clear();
  m_calibration.reset();
  m_hasPreview = false;
  m_preview->hide();
  m_ownsSession = false;
  m_started = true;
  const bool started = m_controller->start(m_alignment, this);
  if (!started) {
    m_started = false;
    m_failed = true;
    m_failureReason =
        m_controller->isActive() ? tr("Another screen calibration is already running.") : m_controller->statusText();
    if (m_failureReason.isEmpty())
      m_failureReason = tr("Calibration could not start. Check that both computers are connected, then retry.");
  } else if (!m_failed) {
    m_ownsSession = m_controller->isActive();
    m_controller->setCheckPointEnabled(m_checkPoint->isChecked());
  }
  updateState();
}

void ScreenCalibrationDialog::updateState()
{
  if (m_finished)
    return;
  const auto endpointText = [this](const QString &name, bool ready) {
    if (m_failed)
      return tr("%1: stopped").arg(name);
    return ready ? tr("%1: ready").arg(name) : tr("%1: waiting for markers…").arg(name);
  };
  m_firstState->setText(
      endpointText(m_alignment.firstName, m_started && m_controller && m_controller->firstEndpoint().ready)
  );
  m_secondState->setText(
      endpointText(m_alignment.secondName, m_started && m_controller && m_controller->secondEndpoint().ready)
  );

  const auto current =
      m_started && m_controller && !m_failed && m_controller->isActive() ? m_controller->result() : std::nullopt;
  if (current) {
    auto preview = m_alignment;
    preview.calibration = current;
    preview.enabled = true;
    m_preview->setAlignment(preview);
    m_preview->show();
    m_hasPreview = true;
    m_previewHint->hide();
  } else {
    m_previewHint->setText(
        m_hasPreview ? tr("Preview shows the last valid alignment. Adjust the markers or retry to continue.")
                     : tr("The preview will appear when both displays have valid marker positions.")
    );
    m_previewHint->show();
  }

  QString measurement = m_started && m_controller && !m_failed ? m_controller->measurementText() : QString();
  if (measurement.isEmpty())
    measurement = tr("No valid measurement yet.");
  m_measurement->setText(measurement);

  QString status = m_failureReason;
  if (!m_failed && m_started && m_controller)
    status = m_controller->statusText();
  if (status.isEmpty())
    status = tr("Waiting for both displays to show the markers.");
  if (!current && m_hasPreview && !m_failed)
    status += '\n' + tr("The displayed preview is the last valid result; it cannot be used yet.");
  m_status->setText(status);
  auto statusPalette = palette();
  if (m_failed || (!current && m_hasPreview)) {
    statusPalette.setColor(
        QPalette::WindowText,
        palette().color(QPalette::Window).lightness() < 128 ? QColor(255, 163, 145) : QColor(166, 45, 26)
    );
  }
  m_status->setPalette(statusPalette);
  m_use->setEnabled(m_ownsSession && m_controller && !m_failed && current && m_controller->canAccept());
  m_checkPoint->setEnabled(m_ownsSession && m_controller && !m_failed && m_controller->isActive());
  m_retry->setVisible(m_failed);
  m_retry->setEnabled(m_controller && m_controller->isAvailable() && !m_controller->isActive());
}

void ScreenCalibrationDialog::sessionCancelled(const QString &reason)
{
  if (m_finished)
    return;
  m_ownsSession = false; // The controller has already aborted and released it.
  m_failed = true;
  m_failureReason = reason.isEmpty() ? tr("Calibration stopped. Retry when both computers are ready.") : reason;
  // Do not reject recursively: the error remains visible and the user can retry
  // or cancel. Our own accept/reject cleanup is guarded by m_finished.
  updateState();
}

void ScreenCalibrationDialog::stopSession()
{
  if (!m_ownsSession)
    return;
  m_ownsSession = false;
  if (m_controller)
    m_controller->cancel();
}

void ScreenCalibrationDialog::accept()
{
  if (m_finished || m_failed || !m_ownsSession || !m_controller || !m_controller->isActive() ||
      !m_controller->canAccept())
    return;
  const auto result = m_controller->result();
  if (!result || result->error() != ScreenEdgeCalibration::Error::None)
    return;
  m_calibration = result;
  m_finished = true;
  stopSession();
  QDialog::accept();
}

void ScreenCalibrationDialog::reject()
{
  if (m_finished)
    return;
  m_calibration.reset();
  m_finished = true;
  stopSession();
  QDialog::reject();
}
