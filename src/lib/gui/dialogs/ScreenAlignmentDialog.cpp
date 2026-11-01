/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ScreenAlignmentDialog.h"

#include "gui/config/ServerConfig.h"
#include "gui/core/ScreenCalibrationController.h"
#include "gui/dialogs/ScreenCalibrationDialog.h"
#include "gui/widgets/ScreenAlignmentPreview.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QVBoxLayout>

#include <cmath>

namespace {
constexpr double minimumSizeMm = 0.1;
constexpr double maximumSizeMm = 100000;

bool editable(const ScreenAlignment &alignment)
{
  if (alignment.calibration)
    return alignment.isValid();
  const auto dimension = [](double value) {
    return std::isfinite(value) && value >= minimumSizeMm && value <= maximumSizeMm;
  };
  return dimension(alignment.firstSizeMm.width()) && dimension(alignment.firstSizeMm.height()) &&
         dimension(alignment.secondSizeMm.width()) && dimension(alignment.secondSizeMm.height()) &&
         std::isfinite(alignment.offsetMm) && std::abs(alignment.offsetMm) <= maximumSizeMm;
}

QLabel *description(const QString &text, QWidget *parent)
{
  auto *label = new QLabel(text, parent);
  label->setWordWrap(true);
  label->setTextFormat(Qt::PlainText);
  return label;
}
} // namespace

ScreenAlignmentDialog::ScreenAlignmentDialog(
    QWidget *parent, ServerConfig &config, const QString &preferredComputer,
    ScreenCalibrationController *calibrationController
)
    : QDialog(parent),
      m_config(config),
      m_calibrationController(calibrationController)
{
  setObjectName("screenAlignmentDialog");
  setWindowTitle(tr("Screen alignment"));
  setMinimumSize(560, 480);
  const QSize available =
      QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->availableGeometry().size() : QSize(880, 820);
  resize(
      qMin(800, qMax(minimumWidth(), available.width() - 80)), qMin(740, qMax(minimumHeight(), available.height() - 80))
  );

  auto *layout = new QVBoxLayout(this);
  layout->setSpacing(12);
  auto *pairRow = new QHBoxLayout;
  auto *pairLabel = new QLabel(tr("Connection:"), this);
  m_pair = new QComboBox(this);
  m_pair->setObjectName("alignmentPair");
  m_pair->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_pair->setMinimumContentsLength(18);
  pairLabel->setBuddy(m_pair);
  pairRow->addWidget(pairLabel);
  pairRow->addWidget(m_pair, 1);
  layout->addLayout(pairRow);
  m_enabled = new QCheckBox(tr("Use physical screen alignment for this connection"), this);
  m_enabled->setObjectName("enableAlignment");
  layout->addWidget(m_enabled);

  auto *scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto *body = new QWidget(scroll);
  auto *bodyLayout = new QVBoxLayout(body);
  bodyLayout->setContentsMargins(0, 0, 6, 0);
  bodyLayout->setSpacing(10);
  bodyLayout->addWidget(description(
      tr("Each rectangle represents one computer desktop. Measure the visible display area, excluding its frame. "
         "Individual monitors within a computer are not arranged here."),
      body
  ));
  auto *methods = new QHBoxLayout;
  m_calibrate = new QPushButton(tr("Calibrate on the actual screens…"), body);
  m_calibrate->setObjectName("calibrateScreenEdges");
  m_calibrate->setToolTip(tr("Match blue A and orange B markers across the two real screen edges. No ruler is needed.")
  );
  m_manual = new QPushButton(tr("Use manual dimensions"), body);
  m_manual->setObjectName("useManualDimensions");
  methods->addWidget(m_calibrate, 1);
  methods->addWidget(m_manual);
  bodyLayout->addLayout(methods);
  m_calibrationSummary = description({}, body);
  m_calibrationSummary->setObjectName("calibrationSummary");
  bodyLayout->addWidget(m_calibrationSummary);
  m_preview = new ScreenAlignmentPreview(body);
  m_preview->setMinimumHeight(200);
  bodyLayout->addWidget(m_preview, 1);

  m_geometry = new QWidget(body);
  auto *geometryLayout = new QVBoxLayout(m_geometry);
  geometryLayout->setContentsMargins(0, 0, 0, 0);
  geometryLayout->setSpacing(10);
  auto *dimensions = new QHBoxLayout;
  const auto createSizeEditor = [&](bool second) {
    auto *group = new QGroupBox(m_geometry);
    auto *form = new QFormLayout(group);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto *width = new QDoubleSpinBox(group);
    auto *height = new QDoubleSpinBox(group);
    width->setObjectName(second ? "secondWidthMm" : "firstWidthMm");
    height->setObjectName(second ? "secondHeightMm" : "firstHeightMm");
    for (auto *spin : {width, height}) {
      spin->setRange(minimumSizeMm, maximumSizeMm);
      spin->setDecimals(1);
      spin->setSingleStep(1);
      spin->setSuffix(tr(" mm"));
      spin->setKeyboardTracking(false);
    }
    form->addRow(tr("Width:"), width);
    form->addRow(tr("Height:"), height);
    auto *swap = new QPushButton(tr("Swap width and height"), group);
    swap->setObjectName(second ? "swapSecondDimensions" : "swapFirstDimensions");
    form->addRow(swap);
    auto *hint = description({}, group);
    form->addRow(hint);
    if (second) {
      m_secondGroup = group;
      m_secondWidth = width;
      m_secondHeight = height;
      m_secondHint = hint;
    } else {
      m_firstGroup = group;
      m_firstWidth = width;
      m_firstHeight = height;
      m_firstHint = hint;
    }
    connect(swap, &QPushButton::clicked, this, [this, second] { swapDimensions(second); });
    dimensions->addWidget(group, 1);
  };
  createSizeEditor(false);
  createSizeEditor(true);
  geometryLayout->addLayout(dimensions);

  auto *offsetRow = new QHBoxLayout;
  m_offsetLabel = new QLabel(m_geometry);
  m_offset = new QDoubleSpinBox(m_geometry);
  m_offset->setObjectName("offsetMm");
  m_offset->setRange(-maximumSizeMm, maximumSizeMm);
  m_offset->setDecimals(1);
  m_offset->setSingleStep(1);
  m_offset->setSuffix(tr(" mm"));
  m_offset->setKeyboardTracking(false);
  m_offsetLabel->setBuddy(m_offset);
  offsetRow->addWidget(m_offsetLabel);
  offsetRow->addWidget(m_offset, 1);
  geometryLayout->addLayout(offsetRow);
  m_offsetHelp = description({}, m_geometry);
  geometryLayout->addWidget(m_offsetHelp);
  auto *presets = new QHBoxLayout;
  m_alignStart = new QPushButton(m_geometry);
  m_alignStart->setObjectName("alignStart");
  auto *center = new QPushButton(tr("Align centers"), m_geometry);
  center->setObjectName("alignCenter");
  m_alignEnd = new QPushButton(m_geometry);
  m_alignEnd->setObjectName("alignEnd");
  presets->addWidget(m_alignStart);
  presets->addWidget(center);
  presets->addWidget(m_alignEnd);
  geometryLayout->addLayout(presets);
  bodyLayout->addWidget(m_geometry);

  scroll->setWidget(body);
  layout->addWidget(scroll, 1);

  // Feedback must remain visible when the geometry editor needs scrolling.
  // In particular, a disabled Save button must never hide its explanation.
  auto *feedback = new QVBoxLayout;
  feedback->setSpacing(4);
  m_mapping = description({}, this);
  m_mapping->setObjectName("alignmentMapping");
  m_mapping->setTextInteractionFlags(Qt::TextSelectableByMouse);
  feedback->addWidget(m_mapping);
  m_status = description({}, this);
  m_status->setObjectName("alignmentStatus");
  feedback->addWidget(m_status);
  layout->addLayout(feedback);

  m_buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  m_buttons->setObjectName("alignmentButtons");
  m_buttons->button(QDialogButtonBox::Save)->setObjectName("saveAlignment");
  m_buttons->button(QDialogButtonBox::Cancel)->setObjectName("cancelAlignment");
  m_reset = m_buttons->addButton(tr("Restore whole edges"), QDialogButtonBox::ResetRole);
  m_reset->setObjectName("resetAlignment");
  layout->addWidget(m_buttons);

  int preferredIndex = 0;
  bool preferredFound = false;
  const auto pairs = config.adjacentPairs();
  for (const auto &pair : pairs) {
    const auto saved = config.screenAlignment(pair);
    m_drafts.append(saved.value_or(initialAlignment(pair)));
    m_unsupported.append(!editable(m_drafts.last()));
    const auto hint = [&](const QString &name) {
      if (saved)
        return tr("Saved dimensions (editable).");
      if (name == config.getServerName() && QGuiApplication::primaryScreen()) {
        const auto size = QGuiApplication::primaryScreen()->physicalSize();
        if (size.width() >= minimumSizeMm && size.width() <= maximumSizeMm && size.height() >= minimumSizeMm &&
            size.height() <= maximumSizeMm)
          return tr("Local display estimate. Check the measurements.");
      }
      return tr("Starting estimate. Remote dimensions are not detected.");
    };
    m_firstHints.append(hint(pair.firstName));
    m_secondHints.append(hint(pair.secondName));
    m_pair->addItem(
        pair.orientation == Qt::Horizontal ? tr("%1 → %2 (left to right)").arg(pair.firstName, pair.secondName)
                                           : tr("%1 → %2 (top to bottom)").arg(pair.firstName, pair.secondName)
    );
    if (!preferredFound && !preferredComputer.isEmpty() &&
        (pair.firstName == preferredComputer || pair.secondName == preferredComputer)) {
      preferredIndex = m_pair->count() - 1;
      preferredFound = true;
    }
  }

  connect(m_pair, &QComboBox::currentIndexChanged, this, &ScreenAlignmentDialog::selectPair);
  connect(m_enabled, &QCheckBox::toggled, this, &ScreenAlignmentDialog::controlsChanged);
  for (auto *spin : {m_firstWidth, m_firstHeight, m_secondWidth, m_secondHeight, m_offset})
    connect(spin, &QDoubleSpinBox::valueChanged, this, &ScreenAlignmentDialog::controlsChanged);
  connect(m_preview, &ScreenAlignmentPreview::offsetEdited, m_offset, &QDoubleSpinBox::setValue);
  connect(m_calibrate, &QPushButton::clicked, this, &ScreenAlignmentDialog::calibrateEdges);
  connect(m_manual, &QPushButton::clicked, this, &ScreenAlignmentDialog::useManualDimensions);
  if (m_calibrationController)
    connect(m_calibrationController, &ScreenCalibrationController::stateChanged, this, [this] { updateFeedback(); });
  connect(m_alignStart, &QPushButton::clicked, this, [this] { alignScreens(0); });
  connect(center, &QPushButton::clicked, this, [this] { alignScreens(1); });
  connect(m_alignEnd, &QPushButton::clicked, this, [this] { alignScreens(2); });
  connect(m_reset, &QPushButton::clicked, this, &ScreenAlignmentDialog::resetAlignment);
  connect(m_buttons, &QDialogButtonBox::accepted, this, &ScreenAlignmentDialog::accept);
  connect(m_buttons, &QDialogButtonBox::rejected, this, &ScreenAlignmentDialog::reject);
  m_pair->setCurrentIndex(preferredIndex);
  selectPair(m_pair->currentIndex());
}

ScreenAlignment ScreenAlignmentDialog::initialAlignment(const ScreenAlignmentPair &pair) const
{
  ScreenAlignment result{pair.firstName, pair.secondName, pair.orientation, QSizeF(600, 340), QSizeF(600, 340)};
  if (auto *screen = QGuiApplication::primaryScreen()) {
    const QSizeF size = screen->physicalSize();
    if (size.width() >= minimumSizeMm && size.width() <= maximumSizeMm && size.height() >= minimumSizeMm &&
        size.height() <= maximumSizeMm) {
      if (pair.firstName == m_config.getServerName())
        result.firstSizeMm = size;
      if (pair.secondName == m_config.getServerName())
        result.secondSizeMm = size;
    }
  }
  return result;
}

void ScreenAlignmentDialog::selectPair(int index)
{
  m_loading = true;
  m_current = index;
  if (index < 0 || index >= m_drafts.size()) {
    m_enabled->setEnabled(false);
    m_geometry->setEnabled(false);
    m_preview->setEnabled(false);
    m_reset->setEnabled(false);
    m_calibrate->setEnabled(false);
    m_manual->setVisible(false);
    m_status->setText(tr("Place two computers next to each other in the layout to align their screens."));
    m_buttons->button(QDialogButtonBox::Save)->setEnabled(false);
    m_loading = false;
    return;
  }
  const auto &draft = m_drafts[index];
  m_enabled->setChecked(draft.enabled);
  m_enabled->setEnabled(!m_unsupported[index]);
  m_firstGroup->setTitle(draft.firstName);
  m_secondGroup->setTitle(draft.secondName);
  m_firstWidth->setValue(draft.firstSizeMm.width());
  m_firstHeight->setValue(draft.firstSizeMm.height());
  m_secondWidth->setValue(draft.secondSizeMm.width());
  m_secondHeight->setValue(draft.secondSizeMm.height());
  m_offset->setValue(draft.offsetMm);
  m_firstHint->setText(m_firstHints[index]);
  m_secondHint->setText(m_secondHints[index]);
  const bool horizontal = draft.orientation == Qt::Horizontal;
  m_offsetLabel->setText(horizontal ? tr("Vertical offset:") : tr("Horizontal offset:"));
  m_offsetHelp->setText(
      horizontal ? tr("Positive moves the second screen down; negative moves it up.")
                 : tr("Positive moves the second screen right; negative moves it left.")
  );
  m_alignStart->setText(horizontal ? tr("Align tops") : tr("Align left edges"));
  m_alignEnd->setText(horizontal ? tr("Align bottoms") : tr("Align right edges"));
  m_loading = false;
  updateFeedback();
}

void ScreenAlignmentDialog::controlsChanged()
{
  if (m_loading || m_current < 0 || m_unsupported[m_current])
    return;
  auto &draft = m_drafts[m_current];
  const auto *changed = sender();
  // Do not round untouched saved values to the spin boxes' display precision.
  if (changed == m_firstWidth)
    draft.firstSizeMm.setWidth(m_firstWidth->value());
  else if (changed == m_firstHeight)
    draft.firstSizeMm.setHeight(m_firstHeight->value());
  else if (changed == m_secondWidth)
    draft.secondSizeMm.setWidth(m_secondWidth->value());
  else if (changed == m_secondHeight)
    draft.secondSizeMm.setHeight(m_secondHeight->value());
  else if (changed == m_offset)
    draft.offsetMm = m_offset->value();
  else if (changed == m_enabled)
    draft.enabled = m_enabled->isChecked();
  if (changed == m_firstWidth || changed == m_firstHeight) {
    m_firstHints[m_current] = tr("Manual dimensions.");
    m_firstHint->setText(m_firstHints[m_current]);
  }
  if (changed == m_secondWidth || changed == m_secondHeight) {
    m_secondHints[m_current] = tr("Manual dimensions.");
    m_secondHint->setText(m_secondHints[m_current]);
  }
  updateFeedback();
}

bool ScreenAlignmentDialog::canSave() const
{
  if (m_drafts.isEmpty())
    return false;
  for (int i = 0; i < m_drafts.size(); ++i) {
    if (m_unsupported[i])
      return false;
    if (m_drafts[i].enabled) {
      const auto intervals = m_drafts[i].intervals();
      if (!intervals || !intervals->hasOverlap)
        return false;
    }
  }
  return true;
}

void ScreenAlignmentDialog::updateFeedback()
{
  if (m_current < 0)
    return;
  const auto &draft = m_drafts[m_current];
  const bool active = draft.enabled && !m_unsupported[m_current];
  m_geometry->setEnabled(active);
  m_geometry->setVisible(!draft.calibration);
  m_calibrate->setEnabled(
      m_calibrationController && m_calibrationController->isAvailable() && !m_calibrationController->isActive()
  );
  m_manual->setVisible(draft.calibration.has_value());
  m_manual->setEnabled(!m_unsupported[m_current]);
  if (draft.calibration) {
    const auto transform = draft.calibration->transform();
    m_calibrationSummary->setText(
        transform
            ? tr("Calibrated from edge markers. Relative edge length 1 : %1; offset %2% of the first edge. The preview "
                 "shows proportions, not millimeters.")
                  .arg(QString::number(transform->ratio, 'g', 5), QString::number(100 * transform->offset, 'f', 2))
            : QString()
    );
  } else {
    m_calibrationSummary->setText(
        tr("Drag matching colored markers on the actual screens, or enter the measurements below.")
    );
  }
  m_preview->setEnabled(active);
  m_preview->setAlignment(draft);
  m_buttons->button(QDialogButtonBox::Save)->setEnabled(canSave());
  const auto intervals = draft.intervals();
  if (!draft.enabled) {
    m_mapping->setText(
        tr("Alignment is off. The whole edge of each computer connects to the whole edge of its neighbor.")
    );
  } else if (intervals && intervals->hasOverlap) {
    const auto range = [](const ScreenAlignmentRange &value) {
      return QStringLiteral("%1% – %2%").arg(value.startPercent, 0, 'g', 6).arg(value.endPercent, 0, 'g', 6);
    };
    m_mapping->setText(
        draft.orientation == Qt::Horizontal
            ? tr("%1 right edge: %2 ↔ %3 left edge: %4\nGreen sections allow crossing; the remaining edge is blocked.")
                  .arg(draft.firstName, range(intervals->first), draft.secondName, range(intervals->second))
            : tr("%1 bottom edge: %2 ↔ %3 top edge: %4\nGreen sections allow crossing; the remaining edge is blocked.")
                  .arg(draft.firstName, range(intervals->first), draft.secondName, range(intervals->second))
    );
  } else {
    m_mapping->setText(tr("There is no usable crossing area for this alignment."));
  }

  QString problem;
  for (int i = 0; i < m_drafts.size(); ++i) {
    const auto &item = m_drafts[i];
    if (m_unsupported[i]) {
      problem =
          tr("Saved values for %1 and %2 exceed this editor's range. Use Restore whole edges before recalibrating.")
              .arg(item.firstName, item.secondName);
      break;
    }
    if (item.enabled) {
      const auto result = item.intervals();
      if (!result || !result->hasOverlap) {
        problem =
            tr("%1 and %2 do not have a usable overlap. Adjust their offset or restore whole edges before saving.")
                .arg(item.firstName, item.secondName);
        break;
      }
    }
  }
  auto textPalette = palette();
  if (!problem.isEmpty())
    textPalette.setColor(
        QPalette::WindowText,
        palette().color(QPalette::Window).lightness() < 128 ? QColor(255, 163, 145) : QColor(166, 45, 26)
    );
  m_status->setPalette(textPalette);
  m_status->setText(problem.isEmpty() ? tr("Changes take effect after you also save Server Configuration.") : problem);
}

void ScreenAlignmentDialog::resetAlignment()
{
  if (m_current < 0)
    return;
  m_drafts[m_current] = initialAlignment(m_drafts[m_current].pair());
  m_unsupported[m_current] = false;
  m_firstHints[m_current] = tr("Editable starting dimensions.");
  m_secondHints[m_current] = tr("Editable starting dimensions.");
  selectPair(m_current);
}

void ScreenAlignmentDialog::alignScreens(int alignment)
{
  if (m_current < 0)
    return;
  const auto &draft = m_drafts[m_current];
  const double difference = draft.orientation == Qt::Horizontal
                                ? draft.firstSizeMm.height() - draft.secondSizeMm.height()
                                : draft.firstSizeMm.width() - draft.secondSizeMm.width();
  m_offset->setValue(difference * alignment / 2);
}

void ScreenAlignmentDialog::swapDimensions(bool second)
{
  if (m_current < 0 || m_unsupported[m_current])
    return;
  auto &draft = m_drafts[m_current];
  if (second) {
    draft.secondSizeMm.transpose();
    m_secondHints[m_current] = tr("Manual dimensions.");
  } else {
    draft.firstSizeMm.transpose();
    m_firstHints[m_current] = tr("Manual dimensions.");
  }
  selectPair(m_current);
}

void ScreenAlignmentDialog::calibrateEdges()
{
  if (!m_calibrationController || m_current < 0)
    return;
  ScreenCalibrationDialog dialog(this, *m_calibrationController, m_drafts[m_current]);
  if (dialog.exec() != QDialog::Accepted || !dialog.calibration())
    return;
  auto &draft = m_drafts[m_current];
  draft.calibration = dialog.calibration();
  draft.enabled = true;
  m_unsupported[m_current] = false;
  selectPair(m_current);
}

void ScreenAlignmentDialog::useManualDimensions()
{
  if (m_current < 0)
    return;
  m_drafts[m_current] = initialAlignment(m_drafts[m_current].pair());
  m_drafts[m_current].enabled = true;
  m_unsupported[m_current] = false;
  m_firstHints[m_current] = tr("Editable starting dimensions.");
  m_secondHints[m_current] = tr("Editable starting dimensions.");
  selectPair(m_current);
}

void ScreenAlignmentDialog::accept()
{
  if (!canSave())
    return;
  auto updated = m_config;
  const auto pairs = updated.adjacentPairs();
  for (const auto &draft : m_drafts) {
    if (!pairs.contains(draft.pair()) || (draft.enabled && !updated.setScreenAlignment(draft))) {
      QMessageBox::warning(
          this, tr("Computer layout changed"), tr("Close Screen alignment and reopen it for the current layout.")
      );
      return;
    }
    if (!draft.enabled)
      updated.clearScreenAlignment(draft.pair());
  }
  // m_config is the outer dialog's staged copy. Neither dialog writes settings
  // while editing; cancelling either level leaves the real configuration alone.
  m_config = updated;
  QDialog::accept();
}
