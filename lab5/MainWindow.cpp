#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "assembler/AssemblerException.h"
#include <QStringList>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // Set word wrap for errors text box (Qt6 way)
    ui->Errors_TextBox->setLineWrapMode(QTextEdit::WidgetWidth);

    sourceCode = QString("PROG  START   0\n"
                        "    JMP     L1 \n"
                        "A1  RESB    10\n"
                        "A2  RESW    20 \n"
                        "B1  WORD    4096 \n"
                        "B2  BYTE    X\"2F4C008A\"\n"
                        "B3  BYTE    C\"Hello!\"\n"
                        "B4  BYTE    128\t\n"
                        "L1  LOADR1  B1\t\n"
                        "    LOADR2  B4\n"
                        "    ADD     R1  R2\n"
                        "    SAVER1  L1\n"
                        "    INT     200\t\n"
                        "    END \n");

    ui->SourceCode_TextBox->setPlainText(sourceCode);

    // Initialize commands text box
    QStringList commandsText;
    for (const Command& cmd : assembler.AvailibleCommands) {
        commandsText.append(QString("%1 %2 %3").arg(cmd.Name, QString::number(cmd.Code, 16).toUpper(), QString::number(cmd.Length, 16).toUpper()));
    }
    ui->Commands_TextBox->setPlainText(commandsText.join("\n"));

    Reset();

    // Connect signals
    connect(ui->ProcessStep_Button, &QPushButton::clicked, this, &MainWindow::ProcessStep_Button_Click);
    connect(ui->Reset_Button, &QPushButton::clicked, this, &MainWindow::Reset_Button_Click);
    connect(ui->Pass_Button, &QPushButton::clicked, this, &MainWindow::Pass_Button_Click);
    connect(ui->SourceCode_TextBox, &QTextEdit::textChanged, this, &MainWindow::SourceCode_TextBox_TextChanged);
    connect(ui->Commands_TextBox, &QTextEdit::textChanged, this, &MainWindow::Commands_TextBox_TextChanged);
    connect(ui->comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::Mode_ComboBox_SelectionChanged);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::ProcessStep_Button_Click()
{
    try {
        assembler.ProcessStep();
        ui->BinaryCode_TextBox->setPlainText(assembler.BinaryCode.join("\n"));

        QStringList tsiText;
        for (const SymbolicName& sn : assembler.TSI) {
            QStringList reqs;
            for (int req : sn.AddressRequirements) {
                reqs.append(QString::number(req, 16).toUpper().rightJustified(6, '0'));
            }
            QString reqsStr = reqs.isEmpty() ? "" : " " + reqs.join(" ");
            QString addressStr;
            if (sn.Address == -1) {
                addressStr = "FFFFFF";
            } else {
                addressStr = QString::number(sn.Address, 16).toUpper().rightJustified(6, '0');
            }
            tsiText.append(QString("%1 %2%3").arg(
                sn.Name,
                addressStr,
                reqsStr));
        }
        ui->TSI_TextBox->setPlainText(tsiText.join("\n"));
        
        // Display TN
        ui->TN_TextBox->setPlainText(assembler.TN.join("\n"));
    } catch (const AssemblerException& ex) {
        ui->Errors_TextBox->setPlainText(QString("Ошибка: %1").arg(ex.getMessage()));
    }

    if (!ui->Errors_TextBox->toPlainText().isEmpty()) {
        ui->ProcessStep_Button->setEnabled(false);
    }
}

void MainWindow::Reset_Button_Click()
{
    Reset();
}

void MainWindow::Reset()
{
    if (ui->ProcessStep_Button == nullptr || ui->TSI_TextBox == nullptr || 
        ui->BinaryCode_TextBox == nullptr || ui->Errors_TextBox == nullptr) {
        return;
    }

    try {
        ui->ProcessStep_Button->setEnabled(true);
        ui->Pass_Button->setEnabled(true);
        ui->TSI_TextBox->clear();
        ui->TN_TextBox->clear();
        ui->BinaryCode_TextBox->clear();
        ui->Errors_TextBox->clear();

        QList<CommandDto> newCommands = Parser::TextToCommandDtos(ui->Commands_TextBox->toPlainText());
        QList<QList<QString>> sourceCode = Parser::ParseCode(ui->SourceCode_TextBox->toPlainText());

        assembler.Reset(sourceCode, newCommands);
    } catch (const AssemblerException& ex) {
        ui->Errors_TextBox->setPlainText(QString("Ошибка: %1").arg(ex.getMessage()));
    }

    if (!ui->Errors_TextBox->toPlainText().isEmpty()) {
        ui->ProcessStep_Button->setEnabled(false);
        ui->Pass_Button->setEnabled(false);
    }
}

void MainWindow::SourceCode_TextBox_TextChanged()
{
    QList<QList<QString>> newSourceCode = Parser::ParseCode(ui->SourceCode_TextBox->toPlainText());

    if (!Comparer::CompareSourceCodeVersions(assembler.SourceCode, newSourceCode)) {
        Reset();
    }
}

void MainWindow::Commands_TextBox_TextChanged()
{
    Reset();
}

void MainWindow::Pass_Button_Click()
{
    while (true) {
        try {
            bool hasFinished = assembler.ProcessStep();

            ui->BinaryCode_TextBox->setPlainText(assembler.BinaryCode.join("\n"));

            QStringList tsiText;
            for (const SymbolicName& sn : assembler.TSI) {
                QStringList reqs;
                for (int req : sn.AddressRequirements) {
                    reqs.append(QString::number(req, 16).toUpper().rightJustified(6, '0'));
                }
                QString reqsStr = reqs.isEmpty() ? "" : " " + reqs.join(" ");
                QString addressStr;
                if (sn.Address == -1) {
                    addressStr = "FFFFFF";
                } else {
                    addressStr = QString::number(sn.Address, 16).toUpper().rightJustified(6, '0');
                }
                tsiText.append(QString("%1 %2%3").arg(
                    sn.Name,
                    addressStr,
                    reqsStr));
            }
            ui->TSI_TextBox->setPlainText(tsiText.join("\n"));
            
            // Display TN
            ui->TN_TextBox->setPlainText(assembler.TN.join("\n"));

            if (hasFinished) {
                break;
            }
        } catch (const AssemblerException& ex) {
            ui->Errors_TextBox->setPlainText(QString("Ошибка: %1").arg(ex.getMessage()));
            break;
        }

        if (!ui->Errors_TextBox->toPlainText().isEmpty()) {
            ui->ProcessStep_Button->setEnabled(false);
            break;
        }
    }
}

void MainWindow::Mode_ComboBox_SelectionChanged()
{
    int index = ui->comboBox->currentIndex();
    
    switch (index) {
        case 0:  // Straight
        {
            sourceCode = QString("PROG  START   0\n"
                            "    JMP     L1 \n"
                            "A1  RESB    10\n"
                            "A2  RESW    20 \n"
                            "B1  WORD    4096 \n"
                            "B2  BYTE    X\"2F4C008A\"\n"
                            "B3  BYTE    C\"Hello!\"\n"
                            "B4  BYTE    128\t\n"
                            "L1  LOADR1  B1\t\n"
                            "    LOADR2  B4\n"
                            "    ADD     R1  R2\n"
                            "    SAVER1  L1\n"
                            "    INT     200\t\n"
                            "    END \n");
            assembler.AddressingMode = "Straight";
            ui->SourceCode_TextBox->setPlainText(sourceCode);
            break;
        }
        
        case 1:  // Relative
        {
            sourceCode = QString("PROG  START   0\n"
                            "    JMP     [L1] \n"
                            "A1  RESB    10\n"
                            "A2  RESW    20 \n"
                            "B1  WORD    4096 \n"
                            "B2  BYTE    X\"2F4C008A\"\n"
                            "B3  BYTE    C\"Hello!\"\n"
                            "B4  BYTE    128\t\n"
                            "L1  LOADR1  [B1]\t\n"
                            "    LOADR2  [B4]\n"
                            "    ADD     R1  R2\n"
                            "    SAVER1  [L1]\n"
                            "    INT     200\t\n"
                            "    END \n");
            assembler.AddressingMode = "Relative";
            ui->SourceCode_TextBox->setPlainText(sourceCode);
            break;
        }
        
        case 2:  // Mixed
        {
            sourceCode = QString("PROG  START   0\n"
                            "    JMP     [L1] \n"
                            "A1  RESB    10\n"
                            "A2  RESW    20 \n"
                            "B1  WORD    4096 \n"
                            "B2  BYTE    X\"2F4C008A\"\n"
                            "B3  BYTE    C\"Hello!\"\n"
                            "B4  BYTE    128\t\n"
                            "L1  LOADR1  B1\t\n"
                            "    LOADR2  [B4]\n"
                            "    ADD     R1  R2\n"
                            "    SAVER1  L1\n"
                            "    INT     200\t\n"
                            "    END \n");
            assembler.AddressingMode = "Mixed";
            ui->SourceCode_TextBox->setPlainText(sourceCode);
            break;
        }
    }
    
    Reset();
}

