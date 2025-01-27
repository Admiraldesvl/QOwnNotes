#include "linkdialog.h"
#include "ui_linkdialog.h"
#include <QKeyEvent>
#include <QDebug>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QFileDialog>
#include <QSettings>
#include <QClipboard>
#include <QMenu>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <utils/misc.h>

LinkDialog::LinkDialog(QString dialogTitle, QWidget *parent) :
        MasterDialog(parent),
        ui(new Ui::LinkDialog) {
    ui->setupUi(this);
    ui->urlEdit->setFocus();
    firstVisibleNoteListRow = 0;

    if (!dialogTitle.isEmpty()) {
        this->setWindowTitle(dialogTitle);
    }

    QStringList nameList = Note::fetchNoteNames();
    ui->searchLineEdit->installEventFilter(this);

    Q_FOREACH(Note note, Note::fetchAll()) {
        auto *item = new QListWidgetItem(note.getName());
        item->setData(Qt::UserRole, note.getId());
        ui->notesListWidget->addItem(item);
    }

    ui->notesListWidget->setCurrentRow(0);

    QClipboard *clipboard = QApplication::clipboard();
    const QString text = clipboard->text();
    const QUrl url(text);

    // set text from clipboard
    if (url.isValid() && !url.scheme().isEmpty()) {
        ui->urlEdit->setText(text);
    }

    setupFileUrlMenu();
}

LinkDialog::~LinkDialog() {
    delete ui;
}

void LinkDialog::on_searchLineEdit_textChanged(const QString &arg1) {
    // search notes when at least 2 characters were entered
    if (arg1.count() >= 2) {
        QList<QString> noteNameList = Note::searchAsNameList(arg1, true);
        this->firstVisibleNoteListRow = -1;

        for (int i = 0; i < this->ui->notesListWidget->count(); ++i) {
            QListWidgetItem *item = this->ui->notesListWidget->item(i);
            if (noteNameList.indexOf(item->text()) < 0) {
                item->setHidden(true);
            } else {
                if (this->firstVisibleNoteListRow < 0) {
                    this->firstVisibleNoteListRow = i;
                }
                item->setHidden(false);
            }
        }
    } else {  // show all items otherwise
        this->firstVisibleNoteListRow = 0;

        for (int i = 0; i < this->ui->notesListWidget->count(); ++i) {
            QListWidgetItem *item = this->ui->notesListWidget->item(i);
            item->setHidden(false);
        }
    }
}

QString LinkDialog::getSelectedNoteName() {
    return ui->notesListWidget->currentRow() > -1
           ? ui->notesListWidget->currentItem()->text() : "";
}

Note LinkDialog::getSelectedNote() {
    if (ui->notesListWidget->currentRow() == -1) {
        return Note();
    }

    const int noteId = ui->notesListWidget->currentItem()->data(Qt::UserRole).toInt();

    return Note::fetch(noteId);
}

QString LinkDialog::getURL() {
    QString url = ui->urlEdit->text().trimmed();

    if (!url.isEmpty() && !url.contains("://")) {
        url = "http://" + url;
    }

    return url;
}

QString LinkDialog::getLinkName() {
    return ui->nameLineEdit->text().trimmed();
}

void LinkDialog::setLinkName(QString text) {
    ui->nameLineEdit->setText(text);
}

QString LinkDialog::getLinkDescription() {
    return ui->descriptionLineEdit->text().trimmed();
}

//
// Event filters on the NoteSearchDialog
//
bool LinkDialog::eventFilter(QObject *obj, QEvent *event) {
    if (obj == ui->searchLineEdit) {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);

            // set focus to the notes list if Key_Down or Key_Tab were pressed
            // in the search line edit
            if ((keyEvent->key() == Qt::Key_Down) ||
                (keyEvent->key() == Qt::Key_Tab)) {
                // choose another selected item if current item is invisible
                QListWidgetItem *item = ui->notesListWidget->currentItem();
                if ((item != nullptr) &&
                    ui->notesListWidget->currentItem()->isHidden() &&
                    (this->firstVisibleNoteListRow >= 0)) {
                    ui->notesListWidget->setCurrentRow(
                            this->firstVisibleNoteListRow);
                }

                // give the keyboard focus to the notes list widget
                ui->notesListWidget->setFocus();
                return true;
            }
        }
        return false;
    } else if (obj == ui->notesListWidget) {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);

            // set focus to the note text edit if Key_Return or Key_Tab
            // were pressed in the notes list
            if ((keyEvent->key() == Qt::Key_Return) ||
                (keyEvent->key() == Qt::Key_Tab)) {
                // focusNoteTextEdit();
                return true;
            }
        }
        return false;
    }

    return MasterDialog::eventFilter(obj, event);
}

void LinkDialog::on_notesListWidget_doubleClicked(const QModelIndex &index) {
    Q_UNUSED(index);
    ui->urlEdit->clear();
    this->close();
    this->setResult(QDialog::Accepted);
}

/**
 * @brief Fetches the title of a webpage
 * @param url
 * @return
 */
QString LinkDialog::getTitleForUrl(const QUrl& url) {
    const QString html = Utils::Misc::downloadUrl(url);

    if (html.isEmpty()) {
        return "";
    }

    // parse title from webpage
    QRegularExpression regex(R"(<title>(.*)<\/title>)", QRegularExpression::MultilineOption |
                             QRegularExpression::DotMatchesEverythingOption |
                             QRegularExpression::InvertedGreedinessOption);
    QRegularExpressionMatch match = regex.match(html);
    QString title = match.captured(1);

    // decode HTML entities
    title = Utils::Misc::unescapeHtml(title);

    // replace some other characters we don't want
    title.replace("[", "(")
            .replace("]", ")")
            .replace("<", "(")
            .replace(">", ")")
            .replace("&#8211;", "-")
            .replace("&#124;", "-")
            .replace("&#038;", "&")
            .replace("&#39;", "'");

    // trim whitespaces and return title
    return title.simplified();
}

/**
 * Selects a local file to link to
 */
void LinkDialog::addFileUrl() {
    QSettings settings;
    // load last url
    QUrl fileUrl = settings.value("LinkDialog/lastSelectedFileUrl").toUrl();

    if (Utils::Misc::isInPortableMode()) {
        fileUrl = QUrl("file://" + Utils::Misc::prependPortableDataPathIfNeeded(
                Utils::Misc::removeIfStartsWith(fileUrl.toLocalFile(), "/")));
    }

    fileUrl = QFileDialog::getOpenFileUrl(this, tr("Select file to link to"),
                                          fileUrl);
    QString fileUrlString = fileUrl.toString(QUrl::FullyEncoded);

    if (Utils::Misc::isInPortableMode()) {
        fileUrlString = "file://" + QUrl("../" +
                Utils::Misc::makePathRelativeToPortableDataPathIfNeeded(
                        fileUrl.toLocalFile())).toString(QUrl::FullyEncoded);
    }

    if (!fileUrlString.isEmpty()) {
        // store url for the next time
        settings.setValue("LinkDialog/lastSelectedFileUrl", fileUrlString);

        // write the file-url to the url text-edit
        ui->urlEdit->setText(fileUrlString);
    }
}

/**
 * Selects a local directory to link to
 */
void LinkDialog::addDirectoryUrl() {
    QSettings settings;
    // load last url
    QUrl directoryUrl = settings.value("LinkDialog/lastSelectedDirectoryUrl").toUrl();

    if (Utils::Misc::isInPortableMode()) {
        directoryUrl = QUrl("file://" + Utils::Misc::prependPortableDataPathIfNeeded(
                Utils::Misc::removeIfStartsWith(directoryUrl.toLocalFile(), "/")));
    }

    directoryUrl = QFileDialog::getExistingDirectoryUrl(this, tr("Select directory to link to"),
                                                        directoryUrl);
    QString directoryUrlString = directoryUrl.toString(QUrl::FullyEncoded);

    if (Utils::Misc::isInPortableMode()) {
        directoryUrlString = "file://" + QUrl("../" +
                Utils::Misc::makePathRelativeToPortableDataPathIfNeeded(
                        directoryUrl.toLocalFile())).toString(QUrl::FullyEncoded);
    }

    if (!directoryUrlString.isEmpty()) {
        // store url for the next time
        settings.setValue("LinkDialog/lastSelectedDirectoryUrl", directoryUrlString);

        // write the directory-url to the url text-edit
        ui->urlEdit->setText(directoryUrlString);
    }
}

void LinkDialog::on_urlEdit_textChanged(const QString &arg1) {
    auto url = QUrl(arg1);

    if (!url.isValid()) {
        return;
    }

    // try to get the title of the webpage if no link name was set
    if (url.scheme().startsWith("http") && ui->nameLineEdit->text().isEmpty()) {
        const QString title = getTitleForUrl(url);

        if (!title.isEmpty()) {
            ui->nameLineEdit->setText(title);
        }
    }
}

void LinkDialog::setupFileUrlMenu() {
    auto *addMenu = new QMenu(this);

    QAction *addFileAction = addMenu->addAction(
            tr("Select file to link to"));
    addFileAction->setIcon(QIcon::fromTheme(
            "document-open",
            QIcon(":icons/breeze-qownnotes/16x16/document-open.svg")));
    connect(addFileAction, SIGNAL(triggered()),
            this, SLOT(addFileUrl()));

    QAction *addDirectoryAction = addMenu->addAction(
            tr("Select directory to link to"));
    addDirectoryAction->setIcon(QIcon::fromTheme(
            "folder",
            QIcon(":icons/breeze-qownnotes/16x16/folder.svg")));
    connect(addDirectoryAction, SIGNAL(triggered()),
            this, SLOT(addDirectoryUrl()));

    ui->fileUrlButton->setMenu(addMenu);
}
