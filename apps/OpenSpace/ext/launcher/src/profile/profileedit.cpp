/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2025                                                               *
 *                                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this  *
 * software and associated documentation files (the "Software"), to deal in the Software *
 * without restriction, including without limitation the rights to use, copy, modify,    *
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to the following   *
 * conditions:                                                                           *
 *                                                                                       *
 * The above copyright notice and this permission notice shall be included in all copies *
 * or substantial portions of the Software.                                              *
 *                                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,   *
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A         *
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT    *
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF  *
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE  *
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                                         *
 ****************************************************************************************/

#include "profile/profileedit.h"

#include "profile/actiondialog.h"
#include "profile/additionalscriptsdialog.h"
#include "profile/assetsdialog.h"
#include "profile/cameradialog.h"
#include "profile/deltatimesdialog.h"
#include "profile/line.h"
#include "profile/marknodesdialog.h"
#include "profile/metadialog.h"
#include "profile/modulesdialog.h"
#include "profile/propertiesdialog.h"
#include "profile/timedialog.h"
#include <openspace/scene/profile.h>
#include <ghoul/format.h>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>
#include <filesystem>
#include <iostream>

using namespace openspace;

namespace {
    QString labelText(size_t size, const QString& title) {
        QString label;
        if (size > 0) {
            label = title + " (" + QString::number(size) + ")";
        }
        else {
            label = title;
        }
        return label;
    }

    std::string summarizeAssets(const std::vector<std::string>& assets) {
        std::string results;
        for (const std::string& a : assets) {
            results += std::format("{}<br>", a);
        }
        return results;
    }

    std::string summarizeKeybindings(const std::vector<Profile::Keybinding>& keybindings,
                                     const std::vector<Profile::Action>& actions)
    {
        std::string results;
        for (const Profile::Keybinding& k : keybindings) {
            const auto it = std::find_if(
                actions.cbegin(), actions.cend(),
                [id = k.action](const Profile::Action& a) { return a.identifier == id; }
            );

            std::string name = it != actions.end() ? it->name : "Unknown action";
            results += std::format("{} ({})<br>", name, ghoul::to_string(k.key));
        }
        return results;
    }

    std::string summarizeProperties(const std::vector<Profile::Property>& properties) {
        std::string results;
        for (openspace::Profile::Property p : properties) {
            results += std::format("{} = {}<br>", p.name, p.value);
        }
        return results;
    }
} // namespace

ProfileEdit::ProfileEdit(Profile& profile, const std::string& profileName,
                         std::filesystem::path assetBasePath,
                         std::filesystem::path userAssetBasePath,
                         std::filesystem::path builtInProfileBasePath,
                         std::filesystem::path profileBasePath,
                         QWidget* parent)
    : QDialog(parent)
    , _profile(profile)
    , _assetBasePath(std::move(assetBasePath))
    , _userAssetBasePath(std::move(userAssetBasePath))
    , _profileBasePath(std::move(profileBasePath))
    , _builtInProfilesPath(std::move(builtInProfileBasePath))
{
    setWindowTitle("Profile Editor");
    createWidgets(profileName);

    initSummaryTextForEachCategory();
}

void ProfileEdit::createWidgets(const std::string& profileName) {
    QBoxLayout* layout = new QVBoxLayout(this);
    QBoxLayout* topLayout = new QHBoxLayout;
    QBoxLayout* leftLayout = new QVBoxLayout;
    {
        QBoxLayout* container = new QHBoxLayout;
        QLabel* profileLabel = new QLabel("Profile Name:");
        profileLabel->setObjectName("profile");
        container->addWidget(profileLabel);

        _profileEdit = new QLineEdit(QString::fromStdString(profileName));
        _profileEdit->setPlaceholderText("required");
        container->addWidget(_profileEdit);

        QPushButton* duplicateButton = new QPushButton("Duplicate Profile");
        connect(
            duplicateButton, &QPushButton::clicked,
            this, &ProfileEdit::duplicateProfile
        );
        container->addWidget(duplicateButton);

        layout->addLayout(container);
    }
    layout->addWidget(new Line);
    {
        QGridLayout* container = new QGridLayout;
        container->setColumnStretch(1, 1);

        _propertiesLabel = new QLabel("Properties");
        _propertiesLabel->setObjectName("heading");
        _propertiesLabel->setWordWrap(true);
        container->addWidget(_propertiesLabel, 0, 0);

        _propertiesEdit = new QTextEdit;
        _propertiesEdit->setReadOnly(true);
        _propertiesEdit->setAccessibleName("Property value settings");
        container->addWidget(_propertiesEdit, 1, 0, 1, 3);

        QPushButton* editProperties = new QPushButton("Edit");
        connect(
            editProperties, &QPushButton::clicked,
            this, &ProfileEdit::openProperties
        );
        editProperties->setAccessibleName("Edit properties");
        container->addWidget(editProperties, 0, 2);

        leftLayout->addLayout(container);
    }
    leftLayout->addWidget(new Line);
    {
        QGridLayout* container = new QGridLayout;
        container->setColumnStretch(1, 1);

        _assetsLabel = new QLabel("Assets");
        _assetsLabel->setObjectName("heading");
        _assetsLabel->setWordWrap(true);
        container->addWidget(_assetsLabel, 0, 0);

        _assetsEdit = new QTextEdit;
        _assetsEdit->setReadOnly(true);
        _assetsEdit->setAccessibleName("Loaded assets");
        container->addWidget(_assetsEdit, 1, 0, 1, 3);

        QPushButton* assetsProperties = new QPushButton("Edit");
        connect(assetsProperties, &QPushButton::clicked, this, &ProfileEdit::openAssets);
        assetsProperties->setAccessibleName("Edit assets");
        container->addWidget(assetsProperties, 0, 2);

        leftLayout->addLayout(container);
    }
    leftLayout->addWidget(new Line);
    {
        QGridLayout* container = new QGridLayout;
        container->setColumnStretch(1, 1);

        _keybindingsLabel = new QLabel("Actions & Keybindings");
        _keybindingsLabel->setObjectName("heading");
        container->addWidget(_keybindingsLabel, 0, 0);

        _keybindingsEdit = new QTextEdit;
        _keybindingsEdit->setReadOnly(true);
        _keybindingsEdit->setAccessibleName("Loaded action and keybindings");
        container->addWidget(_keybindingsEdit, 1, 0, 1, 3);

        QPushButton* keybindingsProperties = new QPushButton("Edit");
        connect(
            keybindingsProperties, &QPushButton::clicked,
            this, &ProfileEdit::openKeybindings
        );
        keybindingsProperties->setAccessibleName("Edit actions and keybindings");
        container->addWidget(keybindingsProperties, 0, 2);

        leftLayout->addLayout(container);
    }
    topLayout->addLayout(leftLayout, 3);

    topLayout->addWidget(new Line);

    QBoxLayout* rightLayout = new QVBoxLayout;
    {
        QBoxLayout* container = new QVBoxLayout;
        _metaLabel = new QLabel("Meta");
        _metaLabel->setObjectName("heading");
        _metaLabel->setWordWrap(true);
        container->addWidget(_metaLabel);

        QPushButton* metaEdit = new QPushButton("Edit");
        connect(metaEdit, &QPushButton::clicked, this, &ProfileEdit::openMeta);
        metaEdit->setLayoutDirection(Qt::RightToLeft);
        metaEdit->setAccessibleName("Edit metadata");
        container->addWidget(metaEdit);
        rightLayout->addLayout(container);
    }
    rightLayout->addWidget(new Line);
    {
        QBoxLayout* container = new QVBoxLayout;
        _interestingNodesLabel = new QLabel("Mark Interesting Nodes");
        _interestingNodesLabel->setObjectName("heading");
        _interestingNodesLabel->setWordWrap(true);
        container->addWidget(_interestingNodesLabel);

        QPushButton* interestingNodesEdit = new QPushButton("Edit");
        connect(
            interestingNodesEdit, &QPushButton::clicked,
            this, &ProfileEdit::openMarkNodes
        );
        interestingNodesEdit->setLayoutDirection(Qt::RightToLeft);
        interestingNodesEdit->setAccessibleName("Edit interesting nodes");
        container->addWidget(interestingNodesEdit);
        rightLayout->addLayout(container);
    }
    rightLayout->addWidget(new Line);
    {
        QBoxLayout* container = new QVBoxLayout;
        _deltaTimesLabel = new QLabel("Simulation Time Increments");
        _deltaTimesLabel->setObjectName("heading");
        _deltaTimesLabel->setWordWrap(true);
        container->addWidget(_deltaTimesLabel);

        QPushButton* deltaTimesEdit = new QPushButton("Edit");
        connect(
            deltaTimesEdit, &QPushButton::clicked,
            this, &ProfileEdit::openDeltaTimes
        );
        deltaTimesEdit->setLayoutDirection(Qt::RightToLeft);
        deltaTimesEdit->setAccessibleName("Edit simulation time increments");
        container->addWidget(deltaTimesEdit);
        rightLayout->addLayout(container);
    }
    rightLayout->addWidget(new Line);
    {
        QBoxLayout* container = new QVBoxLayout;
        _cameraLabel = new QLabel("Camera");
        _cameraLabel->setObjectName("heading");
        _cameraLabel->setWordWrap(true);
        container->addWidget(_cameraLabel);

        QPushButton* cameraEdit = new QPushButton("Edit");
        connect(cameraEdit, &QPushButton::clicked, this, &ProfileEdit::openCamera);
        cameraEdit->setLayoutDirection(Qt::RightToLeft);
        cameraEdit->setAccessibleName("Edit camera");
        container->addWidget(cameraEdit);
        rightLayout->addLayout(container);
    }
    rightLayout->addWidget(new Line);
    {
        QBoxLayout* container = new QVBoxLayout;
        _timeLabel = new QLabel("Time");
        _timeLabel->setObjectName("heading");
        _timeLabel->setWordWrap(true);
        container->addWidget(_timeLabel);

        QPushButton* timeEdit = new QPushButton("Edit");
        connect(timeEdit, &QPushButton::clicked, this, &ProfileEdit::openTime);
        timeEdit->setLayoutDirection(Qt::RightToLeft);
        timeEdit->setAccessibleName("Edit time");
        container->addWidget(timeEdit);
        rightLayout->addLayout(container);
    }
    rightLayout->addWidget(new Line);
    {
        QBoxLayout* container = new QVBoxLayout;
        _modulesLabel = new QLabel("Modules");
        _modulesLabel->setObjectName("heading");
        _modulesLabel->setWordWrap(true);
        container->addWidget(_modulesLabel);

        QPushButton* modulesEdit = new QPushButton("Edit");
        connect(modulesEdit, &QPushButton::clicked, this, &ProfileEdit::openModules);
        modulesEdit->setLayoutDirection(Qt::RightToLeft);
        modulesEdit->setAccessibleName("Edit modules");
        container->addWidget(modulesEdit);
        rightLayout->addLayout(container);
    }
    rightLayout->addWidget(new Line);
    {
        QBoxLayout* container = new QVBoxLayout;
        _additionalScriptsLabel = new QLabel("Additional Scripts");
        _additionalScriptsLabel->setObjectName("heading");
        _additionalScriptsLabel->setWordWrap(true);
        container->addWidget(_additionalScriptsLabel);

        QPushButton* additionalScriptsEdit = new QPushButton("Edit");
        connect(
            additionalScriptsEdit, &QPushButton::clicked,
            this, &ProfileEdit::openAddedScripts
        );
        additionalScriptsEdit->setLayoutDirection(Qt::RightToLeft);
        additionalScriptsEdit->setAccessibleName("Edit additional scripts");
        container->addWidget(additionalScriptsEdit);
        rightLayout->addLayout(container);
    }
    topLayout->addLayout(rightLayout);
    layout->addLayout(topLayout);

    layout->addWidget(new Line);

    {
        QBoxLayout* footer = new QHBoxLayout;
        QDialogButtonBox* buttons = new QDialogButtonBox;
        buttons->setStandardButtons(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, this, &ProfileEdit::approved);
        connect(buttons, &QDialogButtonBox::rejected, this, &ProfileEdit::reject);
        footer->addWidget(buttons);
        layout->addLayout(footer);
    }
}

void ProfileEdit::initSummaryTextForEachCategory() {
    _modulesLabel->setText(labelText(_profile.modules.size(), "Modules"));

    _assetsLabel->setText(labelText(_profile.assets.size(), "Assets"));
    _assetsEdit->setText(QString::fromStdString(summarizeAssets(_profile.assets)));

    _propertiesLabel->setText(labelText(_profile.properties.size(), "Properties"));
    _propertiesEdit->setText(
        QString::fromStdString(summarizeProperties(_profile.properties))
    );

    _keybindingsLabel->setText(
        labelText(_profile.keybindings.size(), "Actions & Keybindings")
    );
    _keybindingsEdit->setText(QString::fromStdString(
        summarizeKeybindings(_profile.keybindings, _profile.actions)
    ));

    _deltaTimesLabel->setText(
        labelText(_profile.deltaTimes.size(), "Simulation Time Increments")
    );
    _interestingNodesLabel->setText(
        labelText(_profile.markNodes.size(), "Mark Interesting Nodes")
    );
}

void ProfileEdit::duplicateProfile() {
    std::string profile = _profileEdit->text().toStdString();
    if (profile.empty()) {
        return;
    }

    constexpr char Separator = '_';
    int version = 0;
    if (const size_t it = profile.rfind(Separator);  it != std::string::npos) {
        // If the value exists, we have a profile that potentially already has a version
        // number attached to it
        const std::string versionStr = profile.substr(it + 1);
        try {
            version = std::stoi(versionStr);

            // We will re-add the separator with the new version string to the file, so we
            // will remove the suffix here first
            profile = profile.substr(0, it);
        }
        catch (const std::invalid_argument&) {
            // If this exception is thrown, we did find a separator character but the
            // substring afterwards was not a number, so the user just added a separator
            // by themselves. In this case we don't do anything
        }
    }

    // By this point we have our current profile (without any suffix) in 'profile' and the
    // currently active version in 'version'. Now we need to put both together again and
    // also make sure that we don't pick a version number that already exists
    while (true) {
        version++;

        const std::string candidate = std::format("{}{}{}", profile, Separator, version);
        const std::string candidatePath = std::format(
            "{}{}.profile", _profileBasePath, candidate
        );

        if (!std::filesystem::exists(candidatePath)) {
            _profileEdit->setText(QString::fromStdString(candidate));
            return;
        }
    }
}

void ProfileEdit::openMeta() {
    MetaDialog(this, &_profile.meta).exec();
}

void ProfileEdit::openModules() {
    ModulesDialog(this, &_profile.modules).exec();
    _modulesLabel->setText(labelText(_profile.modules.size(), "Modules"));
}

void ProfileEdit::openProperties() {
    PropertiesDialog(this, &_profile.properties).exec();
    _propertiesLabel->setText(labelText(_profile.properties.size(), "Properties"));
    _propertiesEdit->setText(
        QString::fromStdString(summarizeProperties(_profile.properties))
    );
}

void ProfileEdit::openKeybindings() {
    ActionDialog(this, &_profile.actions, &_profile.keybindings).exec();
    _keybindingsLabel->setText(labelText(_profile.keybindings.size(), "Keybindings"));
    _keybindingsEdit->setText(QString::fromStdString(
        summarizeKeybindings(_profile.keybindings, _profile.actions)
    ));
}

void ProfileEdit::openAssets() {
    AssetsDialog(this, &_profile, _assetBasePath, _userAssetBasePath).exec();
    _assetsLabel->setText(labelText(_profile.assets.size(), "Assets"));
    _assetsEdit->setText(QString::fromStdString(summarizeAssets(_profile.assets)));
}

void ProfileEdit::openTime() {
    TimeDialog(this, &_profile.time).exec();
}

void ProfileEdit::openDeltaTimes() {
    DeltaTimesDialog(this, &_profile.deltaTimes).exec();
    _deltaTimesLabel->setText(
        labelText(_profile.deltaTimes.size(), "Simulation Time Increments")
    );
}

void ProfileEdit::openAddedScripts() {
    AdditionalScriptsDialog(this, &_profile.additionalScripts).exec();
}

void ProfileEdit::openCamera() {
    CameraDialog(this, &_profile.camera).exec();
}

void ProfileEdit::openMarkNodes() {
    MarkNodesDialog(this, &_profile.markNodes).exec();
    _interestingNodesLabel->setText(
        labelText(_profile.markNodes.size(), "Mark Interesting Nodes")
    );
}

bool ProfileEdit::wasSaved() const {
    return _saveSelected;
}

std::string ProfileEdit::specifiedFilename() const {
    return _profileEdit->text().toStdString();
}

void ProfileEdit::approved() {
    std::string profileName = _profileEdit->text().toStdString();
    if (profileName.empty()) {
        QMessageBox::critical(this, "No profile name", "Profile name must be specified");
        _profileEdit->setFocus();
        return;
    }

    const std::filesystem::path p = std::format(
        "{}/{}.profile", _builtInProfilesPath, profileName
    );
    if (std::filesystem::exists(p)) {
        // The filename exists in the OpenSpace-provided folder, so we don't want to allow
        // a user to overwrite it
        QMessageBox::critical(
            this,
            "Reserved profile name",
            "This is a read-only profile. Click 'Duplicate' or rename profile and save"
        );
        _profileEdit->setFocus();
    }
    else {
        _saveSelected = true;
        accept();
    }
}

void ProfileEdit::keyPressEvent(QKeyEvent* evt) {
    if (evt->key() == Qt::Key_Enter || evt->key() == Qt::Key_Return) {
        return;
    }
    QDialog::keyPressEvent(evt);
}


void ProfileEdit::reject()  {
    // We hijack the reject (i.e., exit window) and emit the signal. The actual shutdown
    // of the window comes at a later stage.
    emit raiseExitWindow();
}

void ProfileEdit::closeWithoutSaving() {
    _saveSelected = false;
    QDialog::reject();
}

void ProfileEdit::promptUserOfUnsavedChanges() {
    QMessageBox msgBox;
    msgBox.setText("There are unsaved changes");
    msgBox.setInformativeText("Do you want to save your changes");
    msgBox.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Save);
    int ret = msgBox.exec();

    switch (ret) {
        case QMessageBox::Save:
            approved();
            break;
        case QMessageBox::Discard:
            closeWithoutSaving();
            break;
        case QMessageBox::Cancel:
            break;
        default:
            break;
    }
}
