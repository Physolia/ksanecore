/* ============================================================
 *
 * SPDX-FileCopyrightText: 2007-2008 Kare Sars <kare dot sars at iki dot fi>
 * SPDX-FileCopyrightText: 2007-2008 Gilles Caulier <caulier dot gilles at gmail dot com>
 * SPDX-FileCopyrightText: 2014 Gregor Mitsch : port to KDE5 frameworks
 * SPDX-FileCopyrightText: 2021 Alexander Stippich <a.stippich@gmx.net>
 *
 * SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
 *
 * ============================================================ */

#include "ksanewidget_p.h"

#include <QImage>
#include <QScrollArea>
#include <QScrollBar>
#include <QList>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QMetaMethod>
#include <QPageSize>

#include <ksane_debug.h>

#include "ksaneinvertoption.h"
#include "ksanepagesizeoption.h"

#define SCALED_PREVIEW_MAX_SIDE 400

static constexpr int ActiveSelection = 100000;
static constexpr int PageSizeWiggleRoom = 2; // in mm

namespace KSaneIface
{

KSaneWidgetPrivate::KSaneWidgetPrivate(KSaneWidget *parent):
    q(parent)
{
    // Device independent UI variables
    m_optsTabWidget = nullptr;
    m_basicOptsTab  = nullptr;
    m_otherOptsTab  = nullptr;
    m_zInBtn        = nullptr;
    m_zOutBtn       = nullptr;
    m_zSelBtn       = nullptr;
    m_zFitBtn       = nullptr;
    m_clearSelBtn   = nullptr;
    m_prevBtn       = nullptr;
    m_scanBtn       = nullptr;
    m_cancelBtn     = nullptr;
    m_previewViewer = nullptr;
    m_autoSelect    = true;
    m_selIndex      = ActiveSelection;
    m_warmingUp     = nullptr;
    m_progressBar   = nullptr;

    // scanning variables
    m_isPreview     = false;

    m_saneHandle    = nullptr;
    m_scanThread    = nullptr;

    m_splitGamChB   = nullptr;
    m_commonGamma   = nullptr;
    m_previewDPI    = 0;

    m_previewWidth  = 0;
    m_previewHeight = 0;

    m_sizeCodes = {
        QPageSize::A4,
        QPageSize::A5,
        QPageSize::A6,
        QPageSize::Letter,
        QPageSize::Legal,
        QPageSize::Tabloid,
        QPageSize::A3,
        QPageSize::B3,
        QPageSize::B4,
        QPageSize::B5,
        QPageSize::B6,
        QPageSize::C5E,
        QPageSize::Comm10E,
        QPageSize::DLE,
        QPageSize::Executive,
        QPageSize::Folio,
        QPageSize::Ledger,
        QPageSize::JisB3,
        QPageSize::JisB4,
        QPageSize::JisB5,
        QPageSize::JisB6,
    };

    clearDeviceOptions();

    m_findDevThread = FindSaneDevicesThread::getInstance();
    connect(m_findDevThread, &FindSaneDevicesThread::finished, this, &KSaneWidgetPrivate::devListUpdated);
    connect(m_findDevThread, &FindSaneDevicesThread::finished, this, &KSaneWidgetPrivate::signalDevListUpdate);

    m_auth = KSaneAuth::getInstance();
    m_optionPollTmr.setInterval(100);
    connect(&m_optionPollTmr, &QTimer::timeout, this, &KSaneWidgetPrivate::pollPollOptions);
}

void KSaneWidgetPrivate::clearDeviceOptions()
{
    m_optSource     = nullptr;
    m_colorOpts     = nullptr;
    m_optNegative   = nullptr;
    m_optFilmType   = nullptr;
    m_optMode       = nullptr;
    m_optDepth      = nullptr;
    m_optRes        = nullptr;
    m_optResX       = nullptr;
    m_optResY       = nullptr;
    m_optTlX        = nullptr;
    m_optTlY        = nullptr;
    m_optBrX        = nullptr;
    m_optBrY        = nullptr;
    m_optGamR       = nullptr;
    m_optGamG       = nullptr;
    m_optGamB       = nullptr;
    m_optPreview    = nullptr;
    m_optWaitForBtn = nullptr;
    m_scanOngoing   = false;

    // delete all the options in the list.
    while (!m_optionsList.isEmpty()) {
        delete m_optionsList.takeFirst();
        delete m_externalOptionsList.takeFirst();
    }
    
    m_optionsLocation.clear();
    m_optionsPollList.clear();
    m_handledOptions.clear();
    m_optionPollTmr.stop();

    // remove the remaining layouts/widgets and read thread
    delete m_basicOptsTab;
    m_basicOptsTab = nullptr;

    delete m_otherOptsTab;
    m_otherOptsTab = nullptr;

    m_devName.clear();
    m_model.clear();
    m_vendor.clear();
    Q_EMIT q->openedDeviceInfoUpdated(m_devName, m_vendor, m_model);
}

void KSaneWidgetPrivate::devListUpdated()
{
    if (m_vendor.isEmpty()) {
        const QList<KSaneWidget::DeviceInfo> deviceList = m_findDevThread->devicesList();
        for (const auto &device : deviceList) {
            if (device.name == m_devName) {
                m_vendor    = device.vendor;
                m_model     = device.model;
                Q_EMIT q->openedDeviceInfoUpdated(m_devName, m_vendor, m_model);
                break;
            }
        }
    }
}

void KSaneWidgetPrivate::signalDevListUpdate()
{
    Q_EMIT q->availableDevices(m_findDevThread->devicesList());
}

KSaneWidget::ImageFormat KSaneWidgetPrivate::getImgFormat(const QImage &image)
{
    switch (image.format()) {
    case QImage::Format_Mono:
        return KSaneWidget::FormatBlackWhite;
    case QImage::Format_Grayscale8:
        return KSaneWidget::FormatGrayScale8;
    case QImage::Format_Grayscale16:
         return KSaneWidget::FormatGrayScale16;
    case QImage::Format_RGB32:
        return KSaneWidget::FormatRGB_8_C;
    case QImage::Format_RGBX64:
        return KSaneWidget::FormatRGB_16_C;
    default:
         return KSaneWidget::FormatNone;
    }
}

float KSaneWidgetPrivate::ratioToScanAreaX(float ratio)
{
    if (!m_optBrX) {
        return 0.0;
    }
    float max = m_optBrX->maximumValue().toFloat();

    return max * ratio;
}

float KSaneWidgetPrivate::ratioToScanAreaY(float ratio)
{
    if (!m_optBrY) {
        return 0.0;
    }
    float max = m_optBrY->maximumValue().toFloat();

    return max * ratio;
}

float KSaneWidgetPrivate::scanAreaToRatioX(float scanArea)
{
    if (!m_optBrX) {
        return 0.0;
    }
    float max = m_optBrX->maximumValue().toFloat();

    if (scanArea > max) {
        return 1.0;
    }

    if (max < 0.0001) {
        return 0.0;
    }

    return scanArea / max;
}

float KSaneWidgetPrivate::scanAreaToRatioY(float scanArea)
{
    if (!m_optBrY) {
        return 0.0;
    }
    float max = m_optBrY->maximumValue().toFloat();

    if (scanArea > max) {
        return 1.0;
    }

    if (max < 0.0001) {
        return 0.0;
    }

    return scanArea / max;
}

static float mmToDispUnit(float mm) {
    static QLocale locale;

    if (locale.measurementSystem() == QLocale::MetricSystem) {
        return mm;
    }
    // else
    return mm / 25.4;
}

float KSaneWidgetPrivate::ratioToDispUnitX(float ratio)
{
    if (!m_optBrX) {
        return 0.0;
    }

    float result = ratioToScanAreaX(ratio);

    if (m_optBrX->valueUnit() == KSaneOption::UnitMilliMeter) {
        return mmToDispUnit(result);
    }
    else if (m_optBrX->valueUnit() == KSaneOption::UnitPixel && m_optRes) {
        // get current DPI
        float dpi = m_optRes->value().toFloat();
        if (dpi > 1) {
            result = result / (dpi / 25.4);
            return mmToDispUnit(result);
        }
    }
    qCDebug(KSANE_LOG) << "Failed to convert scan area to mm";
    return ratio;
}

float KSaneWidgetPrivate::ratioToDispUnitY(float ratio)
{
    if (!m_optBrY) {
        return 0.0;
    }

    float result = ratioToScanAreaY(ratio);

    if (m_optBrY->valueUnit() == KSaneOption::UnitMilliMeter) {
        return mmToDispUnit(result);
    }
    else if (m_optBrY->valueUnit() == KSaneOption::UnitPixel && m_optRes) {
        // get current DPI
        float dpi = m_optRes->value().toFloat();
        if (dpi > 1) {
            result = result / (dpi / 25.4);
            return mmToDispUnit(result);
        }
    }
    qCDebug(KSANE_LOG) << "Failed to convert scan area to display unit";
    return ratio;
}

float KSaneWidgetPrivate::dispUnitToRatioX(float value)
{
    float valueMax = ratioToDispUnitX(1);
    if (valueMax < 1) {
        return 0.0;
    }
    return value / valueMax;
}

float KSaneWidgetPrivate::dispUnitToRatioY(float value)
{
    float valueMax = ratioToDispUnitY(1);
    if (valueMax < 1) {
        return 0.0;
    }
    return value / valueMax;
}

KSaneOptionWidget *KSaneWidgetPrivate::createOptionWidget(QWidget *parent, KSaneOption *option)
{
    KSaneOptionWidget *widget = nullptr;
    switch (option->type()) {
        case KSaneOption::TypeBool:
            widget = new LabeledCheckbox(parent, option);
            break;
        case KSaneOption::TypeInteger:
            widget = new LabeledSlider(parent, option);
            break;
        case KSaneOption::TypeDouble:
            widget = new LabeledFSlider(parent, option);
            break;
        case KSaneOption::TypeValueList:
            widget = new LabeledCombo(parent, option);
            break;
        case KSaneOption::TypeString:
            widget = new LabeledEntry(parent, option);
            break;
        case KSaneOption::TypeGamma:
            widget = new LabeledGamma(parent, option);
            break;
        case KSaneOption::TypeAction:
            widget = new KSaneButton(parent, option);
            break;
        default:
            widget = new KSaneOptionWidget(parent, option);
            break;
    }
    m_handledOptions.insert(option->name());
    return widget;
}

void KSaneWidgetPrivate::createOptInterface()
{
    m_basicOptsTab = new QWidget;
    m_basicScrollA->setWidget(m_basicOptsTab);

    QVBoxLayout *basic_layout = new QVBoxLayout(m_basicOptsTab);
    KSaneOption *option = q->getOption(KSaneWidget::SourceOption);
    // Scan Source
    if (option != nullptr) {
        m_optSource = option;
        KSaneOptionWidget *source = createOptionWidget(m_basicOptsTab, option);
        basic_layout->addWidget(source);
        connect(m_optSource, &KSaneOption::valueChanged, this, &KSaneWidgetPrivate::checkInvert, Qt::QueuedConnection);
        connect(m_optSource, &KSaneOption::valueChanged, this, [this]() {
            m_previewViewer->setMultiselectionEnabled(!scanSourceADF());
        });
    }

    // film-type (note: No translation)
    if ((option = q->getOption(KSaneWidget::FilmTypeOption)) != nullptr) {
        m_optFilmType = option;
        KSaneOptionWidget *film = createOptionWidget(m_basicOptsTab, option);
        basic_layout->addWidget(film);
        connect(m_optFilmType, &KSaneOption::valueChanged, this, &KSaneWidgetPrivate::checkInvert, Qt::QueuedConnection);
    } else if ((option = q->getOption(KSaneWidget::NegativeOption)) != nullptr) {
        m_optNegative = option;
        KSaneOptionWidget *negative = createOptionWidget(m_basicOptsTab, option);
        basic_layout->addWidget(negative);
    }
    // Scan mode
    if ((option = q->getOption(KSaneWidget::ScanModeOption)) != nullptr) {
        m_optMode = option;
        KSaneOptionWidget *mode = createOptionWidget(m_basicOptsTab, option);
        basic_layout->addWidget(mode);
    }
    // Bitdepth
    if ((option = q->getOption(KSaneWidget::BitDepthOption)) != nullptr) {
        m_optDepth = option;
        KSaneOptionWidget *bitDepth = createOptionWidget(m_basicOptsTab, option);
        basic_layout->addWidget(bitDepth);
    }
    // Threshold
    if ((option = q->getOption(KSaneWidget::ThresholdOption)) != nullptr) {
        KSaneOptionWidget *threshold = createOptionWidget(m_basicOptsTab, option);
        basic_layout->addWidget(threshold);
    }
    // Resolution
    if ((option = q->getOption(KSaneWidget::ResolutionOption)) != nullptr) {
        m_optRes = option;
        KSaneOptionWidget *resolution = createOptionWidget(m_basicOptsTab, option);
        basic_layout->addWidget(resolution);
    }
    // These two next resolution options are a bit tricky.
    if ((option = q->getOption(KSaneWidget::XResolutionOption)) != nullptr) {
        m_optResX = option;
        if (!m_optRes) {
            m_optRes = m_optResX;
        }
        KSaneOptionWidget *optResX = createOptionWidget(m_basicOptsTab, option);
        basic_layout->addWidget(optResX);
    }
    if ((option = q->getOption(KSaneWidget::YResolutionOption)) != nullptr) {
        m_optResY = option;
        KSaneOptionWidget *optResY = createOptionWidget(m_basicOptsTab, option);
        basic_layout->addWidget(optResY);
    }

    // save a pointer to the preview option if possible
    if ((option = q->getOption(KSaneWidget::PreviewOption)) != nullptr) {
        m_optPreview = option;
        m_handledOptions.insert(option->name());
    }

    // save a pointer to the "wait-for-button" option if possible (Note: No translation)
    if ((option = q->getOption(KSaneWidget::WaitForButtonOption)) != nullptr) {
        m_optWaitForBtn = option;
        m_handledOptions.insert(option->name());
    }

    // scan area (Do not add the widgets)
    if ((option = q->getOption(KSaneWidget::TopLeftXOption)) != nullptr) {
        m_optTlX = option;
        connect(option, &KSaneOption::valueChanged, this, &KSaneWidgetPrivate::setTLX);
        m_handledOptions.insert(option->name());
    }
    if ((option = q->getOption(KSaneWidget::TopLeftYOption)) != nullptr) {
        m_optTlY = option;
        connect(option, &KSaneOption::valueChanged, this, &KSaneWidgetPrivate::setTLY);
        m_handledOptions.insert(option->name());
    }
    if ((option = q->getOption(KSaneWidget::BottomRightXOption)) != nullptr) {
        m_optBrX = option;
        connect(option, &KSaneOption::valueChanged, this, &KSaneWidgetPrivate::setBRX);
        m_handledOptions.insert(option->name());
    }
    if ((option = q->getOption(KSaneWidget::BottomRightYOption)) != nullptr) {
        m_optBrY = option;
        connect(option, &KSaneOption::valueChanged, this, &KSaneWidgetPrivate::setBRY);
        m_handledOptions.insert(option->name());
    }
    if ((option = q->getOption(KSaneWidget::PageSizeOption)) != nullptr) {
        m_handledOptions.insert(option->name());
    }

    // Color Options Frame
    m_colorOpts = new QWidget(m_basicOptsTab);
    basic_layout->addWidget(m_colorOpts);
    QVBoxLayout *color_lay = new QVBoxLayout(m_colorOpts);
    color_lay->setContentsMargins(0, 0, 0, 0);

    // Add Color correction to the color "frame"
    if ((option = q->getOption(KSaneWidget::BrightnessOption)) != nullptr) {
        KSaneOptionWidget *brightness = createOptionWidget(m_basicOptsTab, option);
        color_lay->addWidget(brightness);
    }
    if ((option = q->getOption(KSaneWidget::ContrastOption)) != nullptr) {
        KSaneOptionWidget *contrast = createOptionWidget(m_basicOptsTab, option);
        color_lay->addWidget(contrast);
    }

    // Add gamma tables to the color "frame"
    QWidget *gamma_frm = new QWidget(m_colorOpts);
    color_lay->addWidget(gamma_frm);
    QVBoxLayout *gam_frm_l = new QVBoxLayout(gamma_frm);
    gam_frm_l->setContentsMargins(0, 0, 0, 0);
    LabeledGamma *gammaR = nullptr;
    LabeledGamma *gammaG = nullptr;
    LabeledGamma *gammaB = nullptr;
    if ((option = q->getOption(KSaneWidget::GammaRedOption)) != nullptr) {
        m_optGamR = option;
        gammaR = new LabeledGamma(gamma_frm, option);
        gam_frm_l->addWidget(gammaR);
        m_handledOptions.insert(option->name());
    }
    if ((option = q->getOption(KSaneWidget::GammaGreenOption)) != nullptr) {
        m_optGamG = option;
        gammaG = new LabeledGamma(gamma_frm, option);
        gam_frm_l->addWidget(gammaG);
        m_handledOptions.insert(option->name());
    }
    if ((option = q->getOption(KSaneWidget::GammaBlueOption)) != nullptr) {
        m_optGamB = option;
        gammaB = new LabeledGamma(gamma_frm, option);
        gam_frm_l->addWidget(gammaB);
        m_handledOptions.insert(option->name());
    }

    if ((m_optGamR != nullptr) && (m_optGamG != nullptr) && (m_optGamB != nullptr) 
        && (gammaR != nullptr) && (gammaG != nullptr) && (gammaB != nullptr)  ) {
        
        m_commonGamma = new LabeledGamma(m_colorOpts, i18n(SANE_TITLE_GAMMA_VECTOR), gammaR->maxValue());

        color_lay->addWidget(m_commonGamma);

        m_commonGamma->setToolTip(i18n(SANE_DESC_GAMMA_VECTOR));

        connect(m_commonGamma, &LabeledGamma::valuesChanged, gammaR, &LabeledGamma::setValues);
        connect(m_commonGamma, &LabeledGamma::valuesChanged, gammaG, &LabeledGamma::setValues);
        connect(m_commonGamma, &LabeledGamma::valuesChanged, gammaB, &LabeledGamma::setValues);

        m_splitGamChB = new LabeledCheckbox(m_colorOpts, i18n("Separate color intensity tables"));
        color_lay->addWidget(m_splitGamChB);

        connect(m_splitGamChB, &LabeledCheckbox::toggled, gamma_frm, &QWidget::setVisible);
        connect(m_splitGamChB, &LabeledCheckbox::toggled, m_commonGamma, &LabeledGamma::setHidden);

        gamma_frm->hide();
    }

    if ((option = q->getOption(KSaneWidget::BlackLevelOption)) != nullptr) {
        KSaneOptionWidget *blackLevel = createOptionWidget(m_colorOpts, option);
        color_lay->addWidget(blackLevel);
    }
    if ((option = q->getOption(KSaneWidget::WhiteLevelOption)) != nullptr) {
        KSaneOptionWidget *blackLevel = createOptionWidget(m_colorOpts, option);
        color_lay->addWidget(blackLevel);
    }
    
    if ((option = q->getOption(KSaneWidget::InvertColorOption)) != nullptr) {
        m_optInvert = option;
        KSaneOptionWidget *invertColor = createOptionWidget(m_colorOpts, option);
        color_lay->addWidget(invertColor);
        connect(m_optInvert, &KSaneOption::valueChanged, this, &KSaneWidgetPrivate::invertPreview);
    }

    // Add our own size options
    m_scanareaPapersize = new LabeledCombo(m_basicOptsTab, i18n("Scan Area Size"));
    connect(m_scanareaPapersize, &LabeledCombo::activated, this, &KSaneWidgetPrivate::setPageSize);
    basic_layout->addWidget(m_scanareaPapersize);

    static QLocale locale;
    QString unitSuffix = locale.measurementSystem() == QLocale::MetricSystem ? i18n(" mm") : i18n(" inch");

    m_scanareaWidth = new LabeledFSlider(m_basicOptsTab, i18n("Width"), 0.0f, 500.0f, 0.1f);
    m_scanareaWidth->setSuffix(unitSuffix);
    connect(m_scanareaWidth, &LabeledFSlider::valueChanged, this, &KSaneWidgetPrivate::updateScanSelection);
    basic_layout->addWidget(m_scanareaWidth);

    m_scanareaHeight = new LabeledFSlider(m_basicOptsTab, i18n("Height"), 0.0f, 500.0f, 0.1f);
    m_scanareaHeight->setSuffix(unitSuffix);
    connect(m_scanareaHeight, &LabeledFSlider::valueChanged, this, &KSaneWidgetPrivate::updateScanSelection);
    basic_layout->addWidget(m_scanareaHeight);

    m_scanareaX = new LabeledFSlider(m_basicOptsTab, i18n("X Offset"), 0.0f, 500.0f, 0.1f);
    m_scanareaX->setSuffix(unitSuffix);
    connect(m_scanareaX, &LabeledFSlider::valueChanged, this, &KSaneWidgetPrivate::updateScanSelection);
    basic_layout->addWidget(m_scanareaX);

    m_scanareaY = new LabeledFSlider(m_basicOptsTab, i18n("Y Offset"), 0.0f, 500.0f, 0.1f);
    m_scanareaY->setSuffix(unitSuffix);
    connect(m_scanareaY, &LabeledFSlider::valueChanged, this, &KSaneWidgetPrivate::updateScanSelection);
    basic_layout->addWidget(m_scanareaY);

    // add a stretch to the end to keep the parameters at the top
    basic_layout->addStretch();

    // Remaining (un known) options go to the "Other Options"
    m_otherOptsTab = new QWidget;
    m_otherScrollA->setWidget(m_otherOptsTab);

    QVBoxLayout *other_layout = new QVBoxLayout(m_otherOptsTab);

    const auto optionsList = q->getOptionsList();
    // add the remaining parameters
    for (const auto option : optionsList) {
        if (m_handledOptions.find(option->name()) != m_handledOptions.end()) {
            continue;
        }
        if (option->type() != KSaneOption::TypeDetectFail) {
            KSaneOptionWidget *widget = createOptionWidget(m_otherOptsTab, option);
            if (widget != nullptr) {
                other_layout->addWidget(widget);
            }
        }
    }

    // add a stretch to the end to keep the parameters at the top
    other_layout->addStretch();

    // calculate label widths
    int labelWidth = 0;
    KSaneOptionWidget *tmpOption;
    // Basic Options
    for (int i = 0; i < basic_layout->count(); ++i) {
        if (basic_layout->itemAt(i) && basic_layout->itemAt(i)->widget()) {
            tmpOption = qobject_cast<KSaneOptionWidget *>(basic_layout->itemAt(i)->widget());
            if (tmpOption) {
                labelWidth = qMax(labelWidth, tmpOption->labelWidthHint());
            }
        }
    }
    // Color Options
    for (int i = 0; i < color_lay->count(); ++i) {
        if (color_lay->itemAt(i) && color_lay->itemAt(i)->widget()) {
            tmpOption = qobject_cast<KSaneOptionWidget *>(color_lay->itemAt(i)->widget());
            if (tmpOption) {
                labelWidth = qMax(labelWidth, tmpOption->labelWidthHint());
            }
        }
    }
    // Set label widths
    for (int i = 0; i < basic_layout->count(); ++i) {
        if (basic_layout->itemAt(i) && basic_layout->itemAt(i)->widget()) {
            tmpOption = qobject_cast<KSaneOptionWidget *>(basic_layout->itemAt(i)->widget());
            if (tmpOption) {
                tmpOption->setLabelWidth(labelWidth);
            }
        }
    }
    for (int i = 0; i < color_lay->count(); ++i) {
        if (color_lay->itemAt(i) && color_lay->itemAt(i)->widget()) {
            tmpOption = qobject_cast<KSaneOptionWidget *>(color_lay->itemAt(i)->widget());
            if (tmpOption) {
                tmpOption->setLabelWidth(labelWidth);
            }
        }
    }
    // Other Options
    labelWidth = 0;
    for (int i = 0; i < other_layout->count(); ++i) {
        if (other_layout->itemAt(i) && other_layout->itemAt(i)->widget()) {
            tmpOption = qobject_cast<KSaneOptionWidget *>(other_layout->itemAt(i)->widget());
            if (tmpOption) {
                labelWidth = qMax(labelWidth, tmpOption->labelWidthHint());
            }
        }
    }
    for (int i = 0; i < other_layout->count(); ++i) {
        if (other_layout->itemAt(i) && other_layout->itemAt(i)->widget()) {
            tmpOption = qobject_cast<KSaneOptionWidget *>(other_layout->itemAt(i)->widget());
            if (tmpOption) {
                tmpOption->setLabelWidth(labelWidth);
            }
        }
    }

    // ensure that we do not get a scrollbar at the bottom of the option of the options
    int min_width = m_basicOptsTab->sizeHint().width();
    if (min_width < m_otherOptsTab->sizeHint().width()) {
        min_width = m_otherOptsTab->sizeHint().width();
    }

    m_optsTabWidget->setMinimumWidth(min_width + m_basicScrollA->verticalScrollBar()->sizeHint().width() + 5);
}

void KSaneWidgetPrivate::setDefaultValues()
{
    KSaneOption *option;

    // Try to get Color mode by default
    if ((option = q->getOption(KSaneWidget::ScanModeOption)) != nullptr) {
        option->setValue(i18n(SANE_VALUE_SCAN_MODE_COLOR));
    }

    // Try to set 8 bit color
    if ((option = q->getOption(KSaneWidget::BitDepthOption)) != nullptr) {
        option->setValue(8);
    }

    // Try to set Scan resolution to 600 DPI
    if (m_optRes != nullptr) {
        m_optRes->setValue(600);
    }
}

void KSaneWidgetPrivate::scheduleValuesReload()
{
    m_readValsTmr.start(5);
}

void KSaneWidgetPrivate::reloadOptions()
{
    for (const auto option : qAsConst(m_optionsList)) {
        option->readOption();
        // Also read the values
        option->readValue();
    }
    // Gamma table special case
    if (m_optGamR && m_optGamG && m_optGamB) {
        m_commonGamma->setHidden(m_optGamR->state() == KSaneOption::StateHidden);
        m_splitGamChB->setHidden(m_optGamR->state() == KSaneOption::StateHidden);
    }

    // estimate the preview size and create an empty image
    // this is done so that you can select scan area without
    // having to scan a preview.
    updatePreviewSize();

    // ensure that we do not get a scrollbar at the bottom of the option of the options
    int min_width = m_basicOptsTab->sizeHint().width();
    if (min_width < m_otherOptsTab->sizeHint().width()) {
        min_width = m_otherOptsTab->sizeHint().width();
    }

    m_optsTabWidget->setMinimumWidth(min_width + m_basicScrollA->verticalScrollBar()->sizeHint().width() + 5);

    m_previewViewer->zoom2Fit();
}

void KSaneWidgetPrivate::reloadValues()
{
    for (const auto option : qAsConst(m_optionsList)) {
        option->readValue();
    }
}

void KSaneWidgetPrivate::handleSelection(float tl_x, float tl_y, float br_x, float br_y)
{
    if ((m_optTlX == nullptr) || (m_optTlY == nullptr) || (m_optBrX == nullptr) || (m_optBrY == nullptr)) {
        // clear the selection since we can not set one
        m_previewViewer->setTLX(0);
        m_previewViewer->setTLY(0);
        m_previewViewer->setBRX(0);
        m_previewViewer->setBRY(0);
        return;
    }

    if ((m_previewImg.width() == 0) || (m_previewImg.height() == 0)) {
        m_scanareaX->setValue(0);
        m_scanareaY->setValue(0);

        m_scanareaWidth->setValue(ratioToDispUnitX(1));
        m_scanareaHeight->setValue(ratioToDispUnitY(1));
       return;
    }

    if (br_x < 0.0001) {
        m_scanareaWidth->setValue(ratioToDispUnitX(1));
        m_scanareaHeight->setValue(ratioToDispUnitY(1));
    }
    else {
        m_scanareaWidth->setValue(ratioToDispUnitX(br_x - tl_x));
        m_scanareaHeight->setValue(ratioToDispUnitY(br_y - tl_y));
    }
    m_scanareaX->setValue(ratioToDispUnitX(tl_x));
    m_scanareaY->setValue(ratioToDispUnitY(tl_y));

    m_optTlX->setValue(ratioToScanAreaX(tl_x));
    m_optTlY->setValue(ratioToScanAreaY(tl_y));
    m_optBrX->setValue(ratioToScanAreaX(br_x));
    m_optBrY->setValue(ratioToScanAreaY(br_y));
}

void KSaneWidgetPrivate::setTLX(const QVariant &x)
{
    bool ok;
    float ftlx = x.toFloat(&ok);
    // ignore this when conversion not possible and during an active scan
    if (!ok || m_scanThread->isRunning() || m_scanOngoing) {
        return;
    }

    float ratio = scanAreaToRatioX(ftlx);
    m_previewViewer->setTLX(ratio);
    m_scanareaX->setValue(ratioToDispUnitX(ratio));
}

void KSaneWidgetPrivate::setTLY(const QVariant &y)
{
    bool ok;
    float ftly = y.toFloat(&ok);
    // ignore this when conversion not possible and during an active scan
    if (!ok || m_scanThread->isRunning() || m_scanOngoing) {
        return;
    }

    float ratio = scanAreaToRatioY(ftly);
    m_previewViewer->setTLY(ratio);
    m_scanareaY->setValue(ratioToDispUnitY(ratio));
}

void KSaneWidgetPrivate::setBRX(const QVariant &x)
{
    bool ok;
    float fbrx = x.toFloat(&ok);
    // ignore this when conversion not possible and during an active scan
    if (!ok || m_scanThread->isRunning() ||  m_scanOngoing) {
        return;
    }

    float ratio = scanAreaToRatioX(fbrx);
    m_previewViewer->setBRX(ratio);

    if (!m_optTlX) {
        return;
    }
    
    QVariant tlx = m_optTlX->value();
    if (!tlx.isNull()) {
        float tlxRatio = scanAreaToRatioX(tlx.toFloat());
        m_scanareaWidth->setValue(ratioToDispUnitX(ratio) - ratioToDispUnitX(tlxRatio));
    }
}

void KSaneWidgetPrivate::setBRY(const QVariant &y)
{
    bool ok;
    float fbry = y.toFloat(&ok);
    // ignore this when conversion not possible and during an active scan
    if (!ok || m_scanThread->isRunning() || m_scanOngoing) {
        return;
    }

    float ratio = scanAreaToRatioY(fbry);
    m_previewViewer->setBRY(ratio);
    
    if (!m_optTlY) {
        return;
    }
    QVariant tly = m_optTlY->value();
    if (!tly.isNull()) {
        float tlyRatio = scanAreaToRatioY(tly.toFloat());
        m_scanareaHeight->setValue(ratioToDispUnitY(ratio) - ratioToDispUnitY(tlyRatio));
    }
}

void KSaneWidgetPrivate::updatePreviewSize()
{
    float max_x = 0;
    float max_y = 0;
    float ratio;
    int x, y;

    // check if an update is necessary
    if (m_optBrX != nullptr) {
        max_x = m_optBrX->maximumValue().toFloat();
    }
    if (m_optBrY != nullptr) {
        max_y = m_optBrY->maximumValue().toFloat();
    }
    if ((max_x == m_previewWidth) && (max_y == m_previewHeight)) {
        //qCDebug(KSANE_LOG) << "no preview size change";
        return;
    }

    // The preview size has changed
    m_previewWidth  = max_x;
    m_previewHeight = max_y;

    // set the scan area to the whole area
    m_previewViewer->clearSelections();
    if (m_optTlX != nullptr) {
        m_optTlX->setValue(0);
    }
    if (m_optTlY != nullptr) {
        m_optTlY->setValue(0);
    }

    if (m_optBrX != nullptr) {
        m_optBrX->setValue(max_x);
    }
    if (m_optBrY != nullptr) {
        m_optBrY->setValue(max_y);
    }

    // Avoid crash if max_y or max_x == 0
    if (max_x < 0.0001 || max_y < 0.0001) {
        qCWarning(KSANE_LOG) << "Risk for division by 0" << max_x << max_y;
        return;
    }

    // create a "scaled" image of the preview
    ratio = max_x / max_y;
    if (ratio < 1) {
        x = SCALED_PREVIEW_MAX_SIDE;
        y = (int)(SCALED_PREVIEW_MAX_SIDE / ratio);
    } else {
        y = SCALED_PREVIEW_MAX_SIDE;
        x = (int)(SCALED_PREVIEW_MAX_SIDE / ratio);
    }

    const qreal dpr = q->devicePixelRatioF();
    m_previewImg = QImage(QSize(x, y) * dpr, QImage::Format_RGB32);
    m_previewImg.setDevicePixelRatio(dpr);
    m_previewImg.fill(0xFFFFFFFF);

    // set the new image
    m_previewViewer->setQImage(&m_previewImg);

    // update the scan-area-options
    m_scanareaWidth->setRange(0.1, ratioToDispUnitX(1));
    m_scanareaWidth->setValue(ratioToDispUnitX(1));

    m_scanareaHeight->setRange(0.1, ratioToDispUnitY(1));
    m_scanareaHeight->setValue(ratioToDispUnitY(1));

    m_scanareaX->setRange(0.0, ratioToDispUnitX(1));
    m_scanareaY->setRange(0.0, ratioToDispUnitY(1));

    setPossibleScanSizes();
}

void KSaneWidgetPrivate::startPreviewScan()
{
    if (m_scanOngoing) {
        return;
    }
    m_scanOngoing = true;

    SANE_Status status;
    float max_x, max_y;
    float dpi;

    // store the current settings of parameters to be changed
    if (m_optDepth != nullptr) {
        m_optDepth->storeCurrentData();
    }
    if (m_optRes != nullptr) {
        m_optRes->storeCurrentData();
    }
    if (m_optResX != nullptr) {
        m_optResX->storeCurrentData();
    }
    if (m_optResY != nullptr) {
        m_optResY->storeCurrentData();
    }
    if (m_optPreview != nullptr) {
        m_optPreview->storeCurrentData();
    }

    // check if we can modify the selection
    if ((m_optTlX != nullptr) && (m_optTlY != nullptr) &&
            (m_optBrX != nullptr) && (m_optBrY != nullptr)) {
        // get maximums
        max_x = m_optBrX->maximumValue().toFloat();
        max_y = m_optBrY->maximumValue().toFloat();
        // select the whole area
        m_optTlX->setValue(0);
        m_optTlY->setValue(0);
        m_optBrX->setValue(max_x);
        m_optBrY->setValue(max_y);

    } else {
        // no use to try auto selections if you can not use them
        m_autoSelect = false;
    }

    if (m_optRes != nullptr) {
        if (m_previewDPI >= 25.0) {
            m_optRes->setValue(m_previewDPI);
            if ((m_optResY != nullptr) && (m_optRes->name() == QStringLiteral(SANE_NAME_SCAN_X_RESOLUTION))) {
                m_optResY->setValue(m_previewDPI);
            }
        } else {
            // set the resolution to minimumValue and increase if necessary
            SANE_Parameters params;
            dpi = m_optRes->minimumValue().toFloat();
            do {
                m_optRes->setValue(dpi);
                if ((m_optResY != nullptr) && (m_optRes->name() == QStringLiteral(SANE_NAME_SCAN_X_RESOLUTION))) {
                    m_optResY->setValue(dpi);
                }
                //check what image size we would get in a scan
                status = sane_get_parameters(m_saneHandle, &params);
                if (status != SANE_STATUS_GOOD) {
                    qCDebug(KSANE_LOG) << "sane_get_parameters=" << sane_strstatus(status);
                    previewScanDone();
                    return;
                }

                if (dpi > 600) {
                    break;
                }

                // Increase the dpi value
                dpi += 25.0;
            } while ((params.pixels_per_line < 300) || ((params.lines > 0) && (params.lines < 300)));

            if (params.pixels_per_line == 0) {
                // This is a security measure for broken backends
                dpi = m_optRes->minimumValue().toFloat();
                m_optRes->setValue(dpi);
                qCDebug(KSANE_LOG) << "Setting minimum DPI value for a broken back-end";
            }
        }
    }

    // set preview option to true if possible
    if (m_optPreview != nullptr) {
        m_optPreview->setValue(SANE_TRUE);
    }

    // execute valReload if there is a pending value reload
    while (m_readValsTmr.isActive()) {
        m_readValsTmr.stop();
        reloadValues();
    }

    // clear the preview
    m_previewViewer->clearHighlight();
    m_previewViewer->clearSelections();
    m_previewImg.fill(0xFFFFFFFF);
    updatePreviewSize();

    setBusy(true);

    m_progressBar->setValue(0);
    m_isPreview = true;
    m_scanThread->start();
}

void KSaneWidgetPrivate::previewScanDone()
{
    // even if the scan is finished successfully we need to call sane_cancel()
    sane_cancel(m_saneHandle);

    // restore the original settings of the changed parameters
    if (m_optDepth != nullptr) {
        m_optDepth->restoreSavedData();
    }
    if (m_optRes != nullptr) {
        m_optRes->restoreSavedData();
    }
    if (m_optResX != nullptr) {
        m_optResX->restoreSavedData();
    }
    if (m_optResY != nullptr) {
        m_optResY->restoreSavedData();
    }
    if (m_optPreview != nullptr) {
        m_optPreview->restoreSavedData();
    }

    m_previewImg = std::move(*m_scanThread->scanImage());
    m_previewViewer->setQImage(&m_previewImg);
    m_previewViewer->zoom2Fit();

    if ((m_scanThread->saneStatus() != SANE_STATUS_GOOD) &&
            (m_scanThread->saneStatus() != SANE_STATUS_EOF)) {
        alertUser(KSaneWidget::ErrorGeneral, sane_i18n(sane_strstatus(m_scanThread->saneStatus())));
    } else if (m_autoSelect) {
        m_previewViewer->findSelections();
    }

    setBusy(false);
    m_scanOngoing = false;

    Q_EMIT q->scanDone(KSaneWidget::NoError, QString());

    return;
}

void KSaneWidgetPrivate::startFinalScan()
{
    if (m_scanOngoing) {
        return;
    }
    m_scanOngoing = true;
    m_isPreview = false;
    m_cancelMultiScan = false;

    float x1 = 0, y1 = 0, x2 = 0, y2 = 0;

    m_selIndex = 0;

    if ((m_optTlX != nullptr) && (m_optTlY != nullptr) && (m_optBrX != nullptr) && (m_optBrY != nullptr)) {
        // read the selection from the viewer
        m_previewViewer->selectionAt(m_selIndex, x1, y1, x2, y2);
        m_previewViewer->setHighlightArea(x1, y1, x2, y2);
        m_selIndex++;

        // now set the selection
        m_optTlX->setValue(ratioToScanAreaX(x1));
        m_optTlY->setValue(ratioToScanAreaY(y1));
        m_optBrX->setValue(ratioToScanAreaX(x2));
        m_optBrY->setValue(ratioToScanAreaY(y2));
    }

    // execute a pending value reload
    while (m_readValsTmr.isActive()) {
        m_readValsTmr.stop();
        reloadValues();
    }

    setBusy(true);
    m_scanThread->start();
}

bool KSaneWidgetPrivate::scanSourceADF()
{
    if (!m_optSource) {
        return false;
    }

    QString source = m_optSource->value().toString();

    return source.contains(QStringLiteral("Automatic Document Feeder")) ||
    source.contains(QStringLiteral("ADF")) ||
    source.contains(QStringLiteral("Duplex"));
}

void KSaneWidgetPrivate::scanDone()
{
    if (m_isPreview) {
        previewScanDone();
    } else {
        oneFinalScanDone();
    }
}

void KSaneWidgetPrivate::oneFinalScanDone()
{

    if (m_scanThread->frameStatus() == KSaneScanThread::ReadReady) {
        // scan finished OK
        Q_EMIT q->scannedImageReady(*m_scanThread->scanImage());

        //TODO: only for compatibility, remove in the future
        if (q->isSignalConnected(QMetaMethod::fromSignal(&KSaneWidget::imageReady))) {
            QImage *scanImage = m_scanThread->scanImage();
            KSaneWidget::ImageFormat format = getImgFormat(*scanImage);
            QByteArray scanData;
            scanData.reserve(scanImage->sizeInBytes());
            switch (format) {
                case KSaneWidget::FormatBlackWhite:
                    scanData = QByteArray::fromRawData(reinterpret_cast<const char*>(scanImage->bits()), scanImage->sizeInBytes());
                    break;
                case KSaneWidget::FormatGrayScale8: {
                    for (int y = 0; y < scanImage->height(); y++) {
                        const uchar *line = scanImage->scanLine(y);
                        for (int x = 0; x < scanImage->width(); x++) {
                            scanData.append(line[x]);
                        }
                    }
                    break;
                }
                case KSaneWidget::FormatGrayScale16: {
                    for (int y = 0; y < scanImage->height(); y++) {
                        const uchar *line = scanImage->scanLine(y);
                        for (int x = 0; x < scanImage->width(); x++) {
                            scanData.append(line[2 * x]);
                            scanData.append(line[2 * x + 1]);
                        }
                    }
                    break;
                }
                case KSaneWidget::FormatRGB_8_C: {
                    for (int y = 0; y < scanImage->height(); y++) {
                        const uchar *line = scanImage->scanLine(y);
                        for (int x = 0; x < scanImage->width(); x++) {
                            scanData.append(line[4 * x]);
                            scanData.append(line[4 * x + 1]);
                            scanData.append(line[4 * x + 2]);
                        }
                    }
                    break;
                }
                case KSaneWidget::FormatRGB_16_C: {
                    for (int y = 0; y < scanImage->height(); y++) {
                        const uchar *line = scanImage->scanLine(y);
                        for (int x = 0; x < scanImage->width(); x++) {
                            scanData.append(line[8 * x]);
                            scanData.append(line[8 * x + 1]);
                            scanData.append(line[8 * x + 2]);
                            scanData.append(line[8 * x + 3]);
                            scanData.append(line[8 * x + 4]);
                            scanData.append(line[8 * x + 5]);
                        }
                    }
                    break;
                }
                default:
                    break;
            }

            Q_EMIT q->imageReady(scanData,
                scanImage->width(),
                scanImage->height(),
                scanImage->bytesPerLine(),
                format);
        }
        // now check if we should have automatic ADF batch scanning
        if (scanSourceADF() && !m_cancelMultiScan) {
            // in batch mode only one area can be scanned per page
            m_scanThread->start();
            m_cancelMultiScan = false;
            return;
        }

        // Check if we have a "wait for button" batch scanning
        if (m_optWaitForBtn) {
            qCDebug(KSANE_LOG) << m_optWaitForBtn->name();
            QString wait = m_optWaitForBtn->value().toString();

            qCDebug(KSANE_LOG) << "wait ==" << wait;
            if (wait == QStringLiteral("true")) {
                // in batch mode only one area can be scanned per page
                //qCDebug(KSANE_LOG) << "source == \"Automatic Document Feeder\"";
                m_scanThread->start();
                return;
            }
        }

        // not batch scan, call sane_cancel to be able to change parameters.
        sane_cancel(m_saneHandle);

        //qCDebug(KSANE_LOG) << "index=" << m_selIndex << "size=" << m_previewViewer->selListSize();
        // check if we have multiple selections.
        if (m_previewViewer->selListSize() > m_selIndex) {
            if ((m_optTlX != nullptr) && (m_optTlY != nullptr) && (m_optBrX != nullptr) && (m_optBrY != nullptr)) {
                float x1 = 0;
                float y1 = 0;
                float x2 = 0;
                float y2 = 0;

                // read the selection from the viewer
                m_previewViewer->selectionAt(m_selIndex, x1, y1, x2, y2);

                // set the highlight
                m_previewViewer->setHighlightArea(x1, y1, x2, y2);

                // now set the selection
                m_optTlX->setValue(ratioToScanAreaX(x1));
                m_optTlY->setValue(ratioToScanAreaY(y1));
                m_optBrX->setValue(ratioToScanAreaX(x2));
                m_optBrY->setValue(ratioToScanAreaY(y2));
                m_selIndex++;

                // execute a pending value reload
                while (m_readValsTmr.isActive()) {
                    m_readValsTmr.stop();
                    reloadValues();
                }

                if (!m_cancelMultiScan) {
                    m_scanThread->start();
                    m_cancelMultiScan = false;
                    return;
                }
            }
        }
        Q_EMIT q->scanDone(KSaneWidget::NoError, QString());
    } else {
        switch (m_scanThread->saneStatus()) {
        case SANE_STATUS_GOOD:
        case SANE_STATUS_CANCELLED:
        case SANE_STATUS_EOF:
            break;
        case SANE_STATUS_NO_DOCS:
            Q_EMIT q->scanDone(KSaneWidget::Information, sane_i18n(sane_strstatus(m_scanThread->saneStatus())));
            alertUser(KSaneWidget::Information, sane_i18n(sane_strstatus(m_scanThread->saneStatus())));
            break;
        case SANE_STATUS_UNSUPPORTED:
        case SANE_STATUS_IO_ERROR:
        case SANE_STATUS_NO_MEM:
        case SANE_STATUS_INVAL:
        case SANE_STATUS_JAMMED:
        case SANE_STATUS_COVER_OPEN:
        case SANE_STATUS_DEVICE_BUSY:
        case SANE_STATUS_ACCESS_DENIED:
            Q_EMIT q->scanDone(KSaneWidget::ErrorGeneral, sane_i18n(sane_strstatus(m_scanThread->saneStatus())));
            alertUser(KSaneWidget::ErrorGeneral, sane_i18n(sane_strstatus(m_scanThread->saneStatus())));
            break;
        }
    }

    sane_cancel(m_saneHandle);

    // clear the highlight
    m_previewViewer->setHighlightArea(0, 0, 1, 1);
    setBusy(false);
    m_scanOngoing = false;
}

void KSaneWidgetPrivate::setBusy(bool busy)
{
    if (busy) {
        m_warmingUp->show();
        m_activityFrame->hide();
        m_btnFrame->hide();
        m_optionPollTmr.stop();
        Q_EMIT q->scanProgress(0);
    } else {
        m_warmingUp->hide();
        m_activityFrame->hide();
        m_btnFrame->show();
        if (m_optionsPollList.size() > 0) {
            m_optionPollTmr.start();
        }
        Q_EMIT q->scanProgress(100);
    }

    m_optsTabWidget->setDisabled(busy);
    m_previewViewer->setDisabled(busy);

    m_scanBtn->setFocus(Qt::OtherFocusReason);
}

void KSaneWidgetPrivate::checkInvert()
{
    if (!m_optSource) {
        return;
    }
    if (!m_optFilmType) {
        return;
    }
    if (m_scanOngoing) {
        return;
    }

    QString source = m_optSource->value().toString();
    QString filmtype = m_optFilmType->value().toString();

    if ((source.contains(i18nc("This is compared to the option string returned by sane",
                               "Transparency"), Qt::CaseInsensitive)) &&
            (filmtype.contains(i18nc("This is compared to the option string returned by sane",
                                     "Negative"), Qt::CaseInsensitive))) {
        m_optInvert->setValue(true);
    } else {
        m_optInvert->setValue(false);
    }
}

void KSaneWidgetPrivate::invertPreview()
{
    m_previewViewer->updateImage();
}

void KSaneWidgetPrivate::updateProgress(int progress)
{
    if (m_isPreview) {
        if (!m_progressBar->isVisible() || m_scanThread->scanImage()->height() != m_previewViewer->currentImageHeight()
            || m_scanThread->scanImage()->width() != m_previewViewer->currentImageWidth() ) {
            m_warmingUp->hide();
            m_activityFrame->show();
            // the image size might have changed
            m_scanThread->lockScanImage();
            m_previewViewer->setQImage(m_scanThread->scanImage());
            m_previewViewer->zoom2Fit();
            m_scanThread->unlockScanImage();
        } else {
            m_scanThread->lockScanImage();
            m_previewViewer->updateImage();
            m_scanThread->unlockScanImage();
        }
    } else {
        if (!m_progressBar->isVisible()) {
            m_warmingUp->hide();
            m_activityFrame->show();
        }
        m_previewViewer->setHighlightShown(progress);
    }

    m_progressBar->setValue(progress);
    Q_EMIT q->scanProgress(progress);
}

void KSaneWidgetPrivate::alertUser(int type, const QString &strStatus)
{
    if (!q->isSignalConnected(QMetaMethod::fromSignal(&KSaneWidget::userMessage))) {
        switch (type) {
        case KSaneWidget::ErrorGeneral:
            QMessageBox::critical(nullptr, i18nc("@title:window", "General Error"), strStatus);
            break;
        default:
            QMessageBox::information(nullptr, i18nc("@title:window", "Information"), strStatus);
            break;
        }
    } else {
        Q_EMIT q->userMessage(type, strStatus);
    }
}

void KSaneWidgetPrivate::pollPollOptions()
{
    for (int i = 1; i < m_optionsPollList.size(); ++i) {
        m_optionsPollList.at(i)->readValue();
    }
}

void KSaneWidgetPrivate::updateScanSelection()
{
    QVariant maxX;
    if (m_optBrX) {
        maxX = m_optBrX->maximumValue();
    }

    QVariant maxY;
    if (m_optBrY) {
        maxY = m_optBrY->maximumValue();
    }

    float x1 = m_scanareaX->value();
    float y1 = m_scanareaY->value();
    float w = m_scanareaWidth->value();
    float h = m_scanareaHeight->value();

    float x1Max = maxX.toFloat() - w;
    m_scanareaX->setRange(0.0, x1Max);
    if (x1 > x1Max) {
        m_scanareaX->setValue(x1Max);
    }

    float y1Max = maxY.toFloat() - h;
    m_scanareaY->setRange(0.0, y1Max);
    if (y1 > y1Max) {
        m_scanareaY->setValue(y1Max);
    }

    float wR = dispUnitToRatioX(w);
    float hR = dispUnitToRatioY(h);

    float x1R = dispUnitToRatioX(m_scanareaX->value());
    float y1R = dispUnitToRatioY(m_scanareaY->value());

    m_previewViewer->setSelection(x1R, y1R, x1R+wR, y1R+hR);

    // Update the page size combo, but not while updating or
    // if we already have custom page size.
    if (m_settingPageSize ||
        m_scanareaPapersize->currentIndex() == m_scanareaPapersize->count() - 1) {
        return;
    }
    QSizeF size = m_scanareaPapersize->currentData().toSizeF();
    float pageWidth = mmToDispUnit(size.width());
    float pageHeight = mmToDispUnit(size.height());
    if (qAbs(pageWidth - w) > (w * 0.001) ||
        qAbs(pageHeight - h) > (h * 0.001))
    {
        // The difference is bigger than 1% -> we have a custom size
        m_scanareaPapersize->blockSignals(true);
        m_scanareaPapersize->setCurrentIndex(0); // Custom is always first
        m_scanareaPapersize->blockSignals(false);
    }
}

void KSaneWidgetPrivate::setPossibleScanSizes()
{
    m_scanareaPapersize->clear();
    float widthInDispUnit = ratioToDispUnitX(1);
    float heightInDispUnit = ratioToDispUnitY(1);

    // Add the custom size first
    QSizeF customSize(widthInDispUnit, heightInDispUnit);
    m_scanareaPapersize->addItem(i18n("Custom"), customSize);

    // Add portrait page sizes
    for (int sizeCode: qAsConst(m_sizeCodes)) {
        QSizeF size = QPageSize::size((QPageSize::PageSizeId)sizeCode, QPageSize::Millimeter);
        if (mmToDispUnit(size.width() - PageSizeWiggleRoom) > widthInDispUnit) {
            continue;
        }
        if (mmToDispUnit(size.height() - PageSizeWiggleRoom) > heightInDispUnit) {
            continue;
        }
        m_scanareaPapersize->addItem(QPageSize::name((QPageSize::PageSizeId)sizeCode), size);
    }

    // Add landscape page sizes
    for (int sizeCode: qAsConst(m_sizeCodes)) {
        QSizeF size = QPageSize::size((QPageSize::PageSizeId)sizeCode, QPageSize::Millimeter);
        size.transpose();
        if (mmToDispUnit(size.width() - PageSizeWiggleRoom) > widthInDispUnit) {
            continue;
        }
        if (mmToDispUnit(size.height() - PageSizeWiggleRoom) > heightInDispUnit) {
            continue;
        }
        QString name = QPageSize::name((QPageSize::PageSizeId)sizeCode) +
        i18nc("Page size landscape", " Landscape");
        m_scanareaPapersize->addItem(name , size);
    }

    // Set custom as current
    m_scanareaPapersize->blockSignals(true);
    m_scanareaPapersize->setCurrentIndex(0);
    m_scanareaPapersize->blockSignals(false);
}

void KSaneWidgetPrivate::setPageSize(int)
{
    QSizeF size = m_scanareaPapersize->currentData().toSizeF();
    float pageWidth = mmToDispUnit(size.width());
    float pageHeight = mmToDispUnit(size.height());

    m_settingPageSize = true;
    m_scanareaX->setValue(0);
    m_scanareaY->setValue(0);
    m_scanareaWidth->setValue(pageWidth);
    m_scanareaHeight->setValue(pageHeight);
    m_settingPageSize = false;
}


}  // NameSpace KSaneIface
