/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "AboutDialog.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("About Meridian"));
    setMinimumWidth(460);
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(12);
    auto *title = new QLabel(QStringLiteral("<span style='font-size:22px;'>Meridian</span>"));
    auto *body = new QLabel(QStringLiteral(
        "<p>A rotator controller for the Green Heron RT-21, written for Andrew, K5PO. "
        "This build is %1.</p>"
        "<p>Designed and tested with firmware 4.13.2, the current public release. "
        "4.14 is a pre-release; on 4.14.g1 the screen saver and register writes hang serial. "
        "Firmware is at "
        "<a href='https://www.greenheronengineering.com/rotator-controllers-software/'>"
        "greenheronengineering.com</a>.</p>")
        .arg(QCoreApplication::applicationVersion()) + QStringLiteral(
        "<p>Meridian is free software under the GNU General Public License, "
        "version 3 or any later version.</p>"
        "<p>The click-to-bearing azimuth and Maidenhead conversion are derived from "
        "<a href='https://github.com/foldynl/QLog'>QLog</a> "
        "(GPL-3.0-or-later, © Thomas Gatzweiler 2020, Ladislav Foldyna 2021–2026).</p>"
        "<p>Coastlines and borders are Natural Earth 1:110m and 1:50m, public domain.</p>"
        "<p>ZIP centroids are the US Census Bureau 2020 ZCTA Gazetteer, public domain. "
        "ZCTAs are not identical to USPS ZIP codes.</p>"
        "<p>The interface uses Inter, © The Inter Project Authors, SIL Open Font License 1.1.</p>"
        "<p>Green Heron Engineering is not affiliated with this project.</p>"));
    body->setWordWrap(true);
    body->setOpenExternalLinks(true);
    body->setTextFormat(Qt::RichText);
    layout->addWidget(title);
    layout->addWidget(body);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(buttons);
}
