#include "Assembler.h"
#include <QRegularExpression>
#include <QSet>
#include <QDebug>

const QStringList Assembler::AvailibleDirectives = {"START", "END", "WORD", "BYTE", "RESB", "RESW"};

Assembler::Assembler()
    : lineIterator(0), startAddress(0), endAddress(0), startFlag(false), endFlag(false), ip(0)
{
    // Default commands
    AvailibleCommands.append(Command(CommandDto("JMP", "1", "4")));
    AvailibleCommands.append(Command(CommandDto("LOADR1", "2", "4")));
    AvailibleCommands.append(Command(CommandDto("LOADR2", "3", "4")));
    AvailibleCommands.append(Command(CommandDto("ADD", "4", "2")));
    AvailibleCommands.append(Command(CommandDto("SAVER1", "5", "4")));
    AvailibleCommands.append(Command(CommandDto("INT", "6", "2")));
}

void Assembler::SetAvailibleCommands(const QList<CommandDto>& newAvailibleCommandsDto)
{
    // Try to convert
    QList<Command> newAvailibleCommands;
    for (const CommandDto& dto : newAvailibleCommandsDto) {
        newAvailibleCommands.append(Command(dto));
    }

    // Check Name uniqueness
    QSet<QString> nhs;
    bool isNameUnique = true;
    for (const Command& cmd : newAvailibleCommands) {
        QString upperName = cmd.Name.toUpper();
        if (nhs.contains(upperName)) {
            isNameUnique = false;
            break;
        }
        nhs.insert(upperName);
    }

    if (!isNameUnique) {
        throw AssemblerException("Все имена команд должны быть уникальными");
    }

    bool isOverlapWithCommands = false;
    for (const Command& cmd : newAvailibleCommands) {
        if (IsDirective(cmd.Name.toUpper()) || IsRegister(cmd.Name.toUpper())) {
            isOverlapWithCommands = true;
            break;
        }
    }

    if (isOverlapWithCommands) {
        throw AssemblerException("Имена команд не должны совпадать с именами директив и регистров");
    }

    // Check Code uniqueness
    QSet<int> chs;
    bool isCodeUnique = true;
    for (const Command& cmd : newAvailibleCommands) {
        if (chs.contains(cmd.Code)) {
            isCodeUnique = false;
            break;
        }
        chs.insert(cmd.Code);
    }

    if (!isCodeUnique) {
        throw AssemblerException("Все коды команд должны быть уникальными");
    }

    this->AvailibleCommands = newAvailibleCommands;
}

void Assembler::Reset(const QList<QList<QString>>& sourceCode, const QList<CommandDto>& newCommands)
{
    SetAvailibleCommands(newCommands);
    ClearTSI();
    SourceCode = sourceCode;
    BinaryCode.clear();

    startAddress = 0;
    endAddress = 0;
    startFlag = false;
    endFlag = false;
    ip = 0;
    lineIterator = 0;
}

bool Assembler::ProcessStep()
{
    if (lineIterator == -1 || endFlag) return true;

    const QList<QString>& line = SourceCode[lineIterator];

    QString textLine = line.join(" ");
    QString binaryCodeLine;

    if (!startFlag && ip != 0) {
        throw AssemblerException("Не найдена директива START в начале программы");
    }

    // Overflow check
    if (startFlag) {
        OverflowCheck(ip, textLine);
    }

    CodeLine codeLine = GetCodeLineFromSource(line);

    // Processing label first
    if (codeLine.hasLabel()) {
        // Try to find label in tsi
        if (startFlag) {
            SymbolicName* symbolicName = GetSymbolicName(codeLine.Label);

            if (symbolicName == nullptr) {
                SymbolicName newSymbolicName;
                newSymbolicName.Name = codeLine.Label.toUpper();
                newSymbolicName.Address = ip;
                TSI.append(newSymbolicName);
            } else {
                if (symbolicName->Address == -1) {
                    symbolicName->Address = ip;
                    ProvideAddresses(symbolicName);
                } else {
                    throw AssemblerException(QString("Такая метка уже есть в ТСИ: %1").arg(textLine));
                }
            }
        }
    }

    // Processing command part
    if (IsDirective(codeLine.Command)) {
        if (codeLine.Command == "START") {
            if (!codeLine.hasFirstOperand()) {
                throw AssemblerException(QString("Не было задано значение адреса начала программы, но адрес начала программы не может быть равен нулю (значение по умолчанию): %1").arg(textLine));
            }

            if (codeLine.hasSecondOperand()) {
                throw AssemblerException(QString("Ожидается один операнд, но найдено два: %1").arg(textLine));
            }

            // Start should be at the beginning and first
            if (ip != 0 || startFlag) {
                throw AssemblerException(QString("START должен быть единственным, в начале исходного кода: %1").arg(textLine));
            }

            // Start was found
            startFlag = true;

            // Process first operand
            int address;
            bool ok;
            address = codeLine.FirstOperand.toInt(&ok, 10);

            if (!ok) {
                throw AssemblerException(QString("Невозможно преобразовать первый операнд в адрес начала программы: %1").arg(textLine));
            }

            // Check if it's within allocated memory bounds
            OverflowCheck(address, textLine);

            if (address == 0) {
                throw AssemblerException(QString("Адрес начала программы не может быть равен нулю: %1").arg(textLine));
            }

            if (!codeLine.hasLabel()) {
                throw AssemblerException("Перед директивой START должна быть метка");
            }

            ip = address;
            startAddress = address;

            // Output
            binaryCodeLine = QString("H %1 %2").arg(codeLine.Label, QString::number(address, 16).toUpper().rightJustified(6, '0'));
        } else if (codeLine.Command == "WORD") {
            // Can only contain a 3-byte unsigned int value
            if (!codeLine.hasFirstOperand()) {
                throw AssemblerException(QString("Ожидается один операнд, но было получено ноль: %1").arg(textLine));
            }
            if (codeLine.hasSecondOperand()) {
                throw AssemblerException(QString("Ожидается один операнд, но найдено два: %1").arg(textLine));
            }

            int value;
            bool ok;
            value = codeLine.FirstOperand.toInt(&ok, 10);

            if (!ok) {
                throw AssemblerException(QString("Невозможно преобразовать первый операнд в число: %1").arg(textLine));
            }

            // Check if within 0-16777215
            if (value <= 0 || value > 16777215) {
                throw AssemblerException(QString("Значение первого операнда выходит за границы допустимого диапазона (1-16777215): %1").arg(textLine));
            }

            // Check for allocated memory overflow
            OverflowCheck(ip + 3, textLine);

            binaryCodeLine = QString("T %1 %2 %3").arg(
                QString::number(ip, 16).toUpper().rightJustified(6, '0'),
                QString::number(3, 16).toUpper().rightJustified(2, '0'),
                QString::number(value, 16).toUpper().rightJustified(6, '0'));
            ip += 3;
        } else if (codeLine.Command == "BYTE") {
            if (!codeLine.hasFirstOperand()) {
                throw AssemblerException(QString("Ожидается один операнд, но было получено ноль: %1").arg(textLine));
            }
            if (codeLine.hasSecondOperand()) {
                throw AssemblerException(QString("Ожидается один операнд, но найдено два: %1").arg(textLine));
            }

            int value;
            bool ok;

            // Try to parse as a 1 byte value
            value = codeLine.FirstOperand.toInt(&ok, 10);
            if (ok) {
                // Check if within 0-255
                if (value < 0 || value > 255) {
                    throw AssemblerException(QString("Значение первого операнда выходит за границы допустимого диапазона (0-255): %1").arg(textLine));
                }

                // Check for allocated memory overflow
                OverflowCheck(ip + 1, textLine);

                binaryCodeLine = QString("T %1 %2 %3").arg(
                    QString::number(ip, 16).toUpper().rightJustified(6, '0'),
                    QString::number(1, 16).toUpper().rightJustified(2, '0'),
                    QString::number(value, 16).toUpper().rightJustified(2, '0'));
                ip += 1;
            } else if (IsCString(codeLine.FirstOperand)) {
                // Couldn't parse as a numeric value => parse as a character string
                QString symbols = codeLine.FirstOperand.mid(2, codeLine.FirstOperand.length() - 3);

                if (symbols.length() > 255) {
                    throw AssemblerException(QString("Длина строки не может превышать 255 байт: %1").arg(textLine));
                }

                // Check for allocated memory overflow
                OverflowCheck(ip + symbols.length(), textLine);

                binaryCodeLine = QString("T %1 %2 %3").arg(
                    QString::number(ip, 16).toUpper().rightJustified(6, '0'),
                    QString::number(symbols.length(), 16).toUpper().rightJustified(2, '0'),
                    ConvertToASCII(symbols));
                ip += symbols.length();
            } else if (IsXString(codeLine.FirstOperand)) {
                QString symbols = codeLine.FirstOperand.mid(2, codeLine.FirstOperand.length() - 3);

                if (symbols.length() / 2 > 255) {
                    throw AssemblerException(QString("Длина строки не может превышать 255 байт: %1").arg(textLine));
                }

                // Check for allocated memory overflow
                OverflowCheck(ip + symbols.length() / 2, textLine);

                binaryCodeLine = QString("T %1 %2 %3").arg(
                    QString::number(ip, 16).toUpper().rightJustified(6, '0'),
                    QString::number(symbols.length() / 2, 16).toUpper().rightJustified(2, '0'),
                    symbols);
                ip += symbols.length() / 2;
            } else {
                throw AssemblerException(QString("Невозможно преобразовать первый операнд в символьную или шестнадцатеричную строку: %1").arg(textLine));
            }
        } else if (codeLine.Command == "RESW") {
            if (!codeLine.hasFirstOperand()) {
                throw AssemblerException(QString("Ожидается один операнд, но было получено ноль: %1").arg(textLine));
            }
            if (codeLine.hasSecondOperand()) {
                throw AssemblerException(QString("Ожидается один операнд, но найдено два: %1").arg(textLine));
            }

            int value;
            bool ok;
            value = codeLine.FirstOperand.toInt(&ok, 10);

            if (!ok) {
                throw AssemblerException(QString("Невозможно преобразовать первый операнд в число: %1").arg(textLine));
            }

            // Check if within 0-16777215
            if (value <= 0 || value > 255) {
                throw AssemblerException(QString("Значение первого операнда выходит за границы допустимого диапазона (1-255): %1").arg(textLine));
            }

            // Check for allocated memory overflow
            OverflowCheck(ip + value * 3, textLine);

            binaryCodeLine = QString("T %1 %2").arg(
                QString::number(ip, 16).toUpper().rightJustified(6, '0'),
                QString::number(value * 3, 16).toUpper().rightJustified(2, '0'));
            ip += value * 3;
        } else if (codeLine.Command == "RESB") {
            if (!codeLine.hasFirstOperand()) {
                throw AssemblerException(QString("Ожидается один операнд, но было получено ноль: %1").arg(textLine));
            }
            if (codeLine.hasSecondOperand()) {
                throw AssemblerException(QString("Ожидается один операнд, но найдено два: %1").arg(textLine));
            }

            int value;
            bool ok;
            value = codeLine.FirstOperand.toInt(&ok, 10);

            if (!ok) {
                throw AssemblerException(QString("Невозможно преобразовать первый операнд в число: %1").arg(textLine));
            }

            // Check if within 0-16777215
            if (value <= 0 || value > 255) {
                throw AssemblerException(QString("Значение первого операнда выходит за границы допустимого диапазона (1-255): %1").arg(textLine));
            }

            // Check for allocated memory overflow
            OverflowCheck(ip + value, textLine);

            binaryCodeLine = QString("T %1 %2").arg(
                QString::number(ip, 16).toUpper().rightJustified(6, '0'),
                QString::number(value, 16).toUpper().rightJustified(2, '0'));
            ip += value;
        } else if (codeLine.Command == "END") {
            if (codeLine.hasSecondOperand()) {
                throw AssemblerException(QString("Ожидается максимум один операнд, но найдено два: %1").arg(textLine));
            }

            if (!startFlag || endFlag) {
                throw AssemblerException(QString("Не найдена метка START либо ошибка в директивах START/END: %1").arg(textLine));
            }

            if (!codeLine.hasFirstOperand()) {
                endAddress = startAddress;
            } else {
                int address;
                bool ok;
                address = codeLine.FirstOperand.toInt(&ok, 10);

                if (!ok) {
                    throw AssemblerException(QString("Невозможно преобразовать первый операнд в адрес входа в программу: %1").arg(textLine));
                }

                if (address < 0 || address > 16777215) {
                    throw AssemblerException(QString("Значение первого операнда выходит за границы допустимого диапазона (0-16777215): %1").arg(textLine));
                }

                if (address < startAddress || address > ip) {
                    throw AssemblerException(QString("Недопустимый адрес входа в программу %1").arg(textLine));
                }

                // Check if it's within allocated memory bounds
                OverflowCheck(address, textLine);

                endAddress = address;
            }

            int progLength = ip - startAddress;

            if (!BinaryCode.isEmpty()) {
                BinaryCode[0] = QString("%1 %2").arg(BinaryCode[0], QString::number(progLength, 16).toUpper().rightJustified(6, '0'));
            }

            endFlag = true;

            binaryCodeLine = QString("E %1").arg(QString::number(endAddress, 16).toUpper().rightJustified(6, '0'));

            CheckAddressRequirements();
        }
    } else if (IsCommand(codeLine.Command)) {
        Command* command = nullptr;
        for (int i = 0; i < AvailibleCommands.size(); i++) {
            if (AvailibleCommands[i].Name.toUpper() == codeLine.Command.toUpper()) {
                command = &AvailibleCommands[i];
                break;
            }
        }

        if (command == nullptr) {
            throw AssemblerException(QString("Команда не найдена: %1").arg(textLine));
        }

        if (command->Length == 1) {
            // Length is 1 (operandless)
            if (codeLine.hasFirstOperand()) {
                throw AssemblerException(QString("Ожидается ноль операндов: %1").arg(textLine));
            }

            // Check for allocated memory overflow
            OverflowCheck(ip + 1, textLine);

            // Addressing type 00
            binaryCodeLine = QString("T %1 %2 %3").arg(
                QString::number(ip, 16).toUpper().rightJustified(6, '0'),
                QString::number(command->Length, 16).toUpper().rightJustified(2, '0'),
                QString::number(command->Code * 4 + 0, 16).toUpper().rightJustified(2, '0'));

            ip += 1;
        } else if (command->Length == 2) {
            // Length is 2
            // Either two registers as two operands
            // or one 1-byte value
            if (!codeLine.hasFirstOperand()) {
                throw AssemblerException(QString("Ожидается минимум один операнд, но было получено ноль: %1").arg(textLine));
            }

            // Two registers
            if (codeLine.hasSecondOperand()) {
                if (IsRegister(codeLine.FirstOperand) && IsRegister(codeLine.SecondOperand)) {
                    // Check for allocated memory overflow
                    OverflowCheck(ip + 2, textLine);

                    // Addressing type 00
                    binaryCodeLine = QString("T %1 %2 %3%4%5").arg(
                        QString::number(ip, 16).toUpper().rightJustified(6, '0'),
                        QString::number(command->Length, 16).toUpper().rightJustified(2, '0'),
                        QString::number(command->Code * 4 + 0, 16).toUpper().rightJustified(2, '0'),
                        QString::number(GetRegisterNumber(codeLine.FirstOperand), 16).toUpper().rightJustified(1, '0'),
                        QString::number(GetRegisterNumber(codeLine.SecondOperand), 16).toUpper().rightJustified(1, '0'));

                    ip += 2;
                } else {
                    throw AssemblerException(QString("Неверный формат команды. Ожидалось два регистра: %1").arg(textLine));
                }
            } else {
                // 1-byte value
                int value;
                bool ok;
                value = codeLine.FirstOperand.toInt(&ok, 10);

                if (!ok) {
                    throw AssemblerException(QString("Невозможно преобразовать первый операнд в число: %1").arg(textLine));
                }

                // Check if within 0-255
                if (value < 0 || value > 255) {
                    throw AssemblerException(QString("Значение первого операнда выходит за границы допустимого диапазона (0-255): %1").arg(textLine));
                }

                // Check for allocated memory overflow
                OverflowCheck(ip + 2, textLine);

                // Addressing type 00
                binaryCodeLine = QString("T %1 %2 %3%4").arg(
                    QString::number(ip, 16).toUpper().rightJustified(6, '0'),
                    QString::number(command->Length, 16).toUpper().rightJustified(2, '0'),
                    QString::number(command->Code * 4 + 0, 16).toUpper().rightJustified(2, '0'),
                    QString::number(value, 16).toUpper().rightJustified(2, '0'));

                ip += 2;
            }
        } else if (command->Length == 4) {
            // Length 4
            if (!codeLine.hasFirstOperand()) {
                throw AssemblerException(QString("Ожидается один операнд, но было получено ноль: %1").arg(textLine));
            }
            if (codeLine.hasSecondOperand()) {
                throw AssemblerException(QString("Ожидается один операнд, но найдено два: %1").arg(textLine));
            }

            // Check for allocated memory overflow
            OverflowCheck(ip + 4, textLine);

            // Is it a label?
            if (IsLabel(codeLine.FirstOperand)) {
                SymbolicName* symbolicName = GetSymbolicName(codeLine.FirstOperand);

                if (symbolicName == nullptr) {
                    SymbolicName newSymbolicName;
                    newSymbolicName.Name = codeLine.FirstOperand.toUpper();
                    newSymbolicName.AddressRequirements.append(ip);
                    TSI.append(newSymbolicName);

                    QString negativeOne = QString::number(0xFFFFFF, 16).toUpper().rightJustified(6, '0');
                    binaryCodeLine = QString("T %1 %2  %3%4").arg(
                        QString::number(ip, 16).toUpper().rightJustified(6, '0'),
                        QString::number(command->Length, 16).toUpper().rightJustified(2, '0'),
                        QString::number(command->Code * 4 + 1, 16).toUpper().rightJustified(2, '0'),
                        negativeOne);
                } else {
                    if (symbolicName->Address == -1) {
                        // Undefined, push ip to AddressRequirements
                        symbolicName->AddressRequirements.append(ip);
                        QString negativeOne = QString::number(0xFFFFFF, 16).toUpper().rightJustified(6, '0');
                        binaryCodeLine = QString("T %1 %2  %3%4").arg(
                            QString::number(ip, 16).toUpper().rightJustified(6, '0'),
                            QString::number(command->Length, 16).toUpper().rightJustified(2, '0'),
                            QString::number(command->Code * 4 + 1, 16).toUpper().rightJustified(2, '0'),
                            negativeOne);
                    } else {
                        // Defined
                        binaryCodeLine = QString("T %1 %2  %3%4").arg(
                            QString::number(ip, 16).toUpper().rightJustified(6, '0'),
                            QString::number(command->Length, 16).toUpper().rightJustified(2, '0'),
                            QString::number(command->Code * 4 + 1, 16).toUpper().rightJustified(2, '0'),
                            QString::number(symbolicName->Address, 16).toUpper().rightJustified(6, '0'));
                    }
                }

                ip += 4;
            } else {
                // Is it a parsable 3-byte value?
                bool ok;
                int value = codeLine.FirstOperand.toInt(&ok, 10);
                if (ok) {
                    if (value < 0 || value > 16777215) {
                        throw AssemblerException(QString("Недопустимое значение операнда: %1").arg(textLine));
                    }

                    // Addressing type 01
                    binaryCodeLine = QString("T %1 %2 %3%4").arg(
                        QString::number(ip, 16).toUpper().rightJustified(6, '0'),
                        QString::number(command->Length, 16).toUpper().rightJustified(2, '0'),
                        QString::number(command->Code * 4, 16).toUpper().rightJustified(2, '0'),
                        QString::number(value, 16).toUpper().rightJustified(6, '0'));

                    ip += 4;
                } else {
                    throw AssemblerException(QString("Недопустимое значение операнда: %1").arg(textLine));
                }
            }
        }
    } else {
        throw AssemblerException(QString("Неизвестная команда: %1").arg(textLine));
    }

    BinaryCode.append(binaryCodeLine);

    lineIterator++;

    if (lineIterator >= SourceCode.size()) {
        lineIterator = -1;
    }

    if (lineIterator == -1 && !endFlag) {
        throw AssemblerException("Не найдена точка входа в программу.");
    }

    return false;
}

void Assembler::CheckAddressRequirements()
{
    for (const SymbolicName& sn : TSI) {
        if (!sn.AddressRequirements.isEmpty()) {
            throw AssemblerException("Не всем меткам было присвоено значение");
        }
    }
}

void Assembler::ProvideAddresses(SymbolicName* symbolicName)
{
    for (int requirement : symbolicName->AddressRequirements) {
        // Find T lines (not H lines)
        for (int i = 0; i < BinaryCode.size(); i++) {
            QString line = BinaryCode[i];
            QStringList parts = line.split(' ');
            if (parts.size() >= 2 && parts[0] != "H") {
                bool ok;
                int address = parts[1].toInt(&ok, 16);
                if (ok && address == requirement) {
                    // Replace last 6 characters with new address
                    QString newLine = line.left(line.length() - 6) + QString::number(symbolicName->Address, 16).toUpper().rightJustified(6, '0');
                    BinaryCode[i] = newLine;
                    break;
                }
            }
        }
    }

    symbolicName->AddressRequirements.clear();
}

void Assembler::ClearTSI()
{
    TSI.clear();
}

bool Assembler::IsCommand(const QString& chunk) const
{
    if (chunk.isEmpty()) return false;

    QString upperChunk = chunk.toUpper();
    for (const Command& cmd : AvailibleCommands) {
        if (cmd.Name.toUpper() == upperChunk) {
            return true;
        }
    }
    return false;
}

bool Assembler::IsDirective(const QString& chunk) const
{
    if (chunk.isEmpty()) return false;

    return AvailibleDirectives.contains(chunk.toUpper());
}

bool Assembler::IsLabel(const QString& chunk) const
{
    if (chunk.isEmpty()) return false;

    if (chunk.length() > 10) return false;

    QString firstChar = chunk.left(1);
    if (!firstChar.contains(QRegularExpression("[a-zA-Z]"))) return false;

    QRegularExpression labelRegex("^[a-zA-Z0-9_]+$");
    if (!labelRegex.match(chunk).hasMatch()) return false;

    if (IsRegister(chunk.toUpper())) return false;

    if (IsCommand(chunk) || IsDirective(chunk)) return false;

    return true;
}

bool Assembler::IsXString(const QString& chunk)
{
    if (chunk.isEmpty()) return false;

    if (!chunk.startsWith("X\"", Qt::CaseInsensitive) || !chunk.endsWith("\"")) {
        return false;
    }

    QString symbols = chunk.mid(2, chunk.length() - 3).toUpper();

    if (symbols.length() < 1 || symbols.contains("\"") || symbols.length() % 2 != 0) {
        return false;
    }

    QRegularExpression hexRegex("^[0-9A-F]+$");
    if (!hexRegex.match(symbols).hasMatch()) {
        return false;
    }

    return true;
}

bool Assembler::IsCString(const QString& chunk)
{
    if (chunk.isEmpty()) return false;

    if (!chunk.startsWith("C\"", Qt::CaseInsensitive) || !chunk.endsWith("\"") || chunk.length() < 4) {
        return false;
    }

    QString symbols = chunk.mid(1, chunk.length() - 1);

    if (symbols.length() < 1) {
        return false;
    }

    // Check if all characters are ASCII (0-127)
    for (QChar c : symbols) {
        if (c.unicode() > 127) {
            return false;
        }
    }

    return true;
}

bool Assembler::IsRegister(const QString& chunk)
{
    if (chunk.isEmpty()) return false;

    QRegularExpression regex("^R([1-9]|1[0-6])$");
    return regex.match(chunk).hasMatch();
}

int Assembler::GetRegisterNumber(const QString& chunk)
{
    QString numberStr = chunk.mid(1);
    return numberStr.toInt() - 1;
}

SymbolicName* Assembler::GetSymbolicName(const QString& chunk)
{
    QString upperChunk = chunk.toUpper();
    for (int i = 0; i < TSI.size(); i++) {
        if (TSI[i].Name.toUpper() == upperChunk) {
            return &TSI[i];
        }
    }
    return nullptr;
}

QString Assembler::ConvertToASCII(const QString& chunk)
{
    QString result;
    QByteArray textBytes = chunk.toLatin1();
    for (int i = 0; i < textBytes.length(); i++) {
        result += QString::number(static_cast<unsigned char>(textBytes[i]), 16).toUpper().rightJustified(2, '0');
    }
    return result;
}

void Assembler::OverflowCheck(int value, const QString& textLine)
{
    if (value < 0 || value > maxAddress) {
        throw AssemblerException(QString("Произошло переполнение выделенной памяти: %1").arg(textLine));
    }
}

CodeLine Assembler::GetCodeLineFromSource(const QList<QString>& line)
{
    QString textLine = line.join(" ");

    if (line.size() < 1 || line.size() > 4) {
        throw AssemblerException(QString("Неверный формат команды: %1").arg(textLine));
    }

    CodeLine codeLine;

    switch (line.size()) {
        case 1:
            // Can only be an operand-less command or END
            if (IsCommand(line[0]) || line[0].toUpper() == "END") {
                codeLine.Label = "";
                codeLine.Command = line[0].toUpper();
                codeLine.FirstOperand = "";
                codeLine.SecondOperand = "";
            } else {
                throw AssemblerException(QString("Неверный формат команды: %1").arg(textLine));
            }
            break;

        case 2:
            // Can be a label and an operand-less command or start/end
            if (IsRegister(line[0].toUpper())) {
                throw AssemblerException(QString("Регистр не может использоваться в качестве метки: %1").arg(textLine));
            } else if (IsLabel(line[0]) && (IsCommand(line[1]) || line[1].toUpper() == "START" || line[1].toUpper() == "END")) {
                codeLine.Label = line[0].toUpper();
                codeLine.Command = line[1].toUpper();
                codeLine.FirstOperand = "";
                codeLine.SecondOperand = "";
            } else if (IsCommand(line[0]) || IsDirective(line[0])) {
                // Can be a command with one operand
                // or a keyword with one operand
                codeLine.Label = "";
                codeLine.Command = line[0].toUpper();
                codeLine.FirstOperand = line[1];
                codeLine.SecondOperand = "";
            } else {
                throw AssemblerException(QString("Неверный формат команды: %1").arg(textLine));
            }
            break;

        case 3:
            // Can be a label and a keyword with one operand
            // can be a command with two operands
            if (IsRegister(line[0].toUpper())) {
                throw AssemblerException(QString("Регистр не может использоваться в качестве метки: %1").arg(textLine));
            } else if (IsLabel(line[0]) && (IsCommand(line[1]) || IsDirective(line[1]))) {
                codeLine.Label = line[0].toUpper();
                codeLine.Command = line[1].toUpper();
                codeLine.FirstOperand = line[2];
                codeLine.SecondOperand = "";
            } else if (IsCommand(line[0])) {
                codeLine.Label = "";
                codeLine.Command = line[0].toUpper();
                codeLine.FirstOperand = line[1];
                codeLine.SecondOperand = line[2];
            } else {
                throw AssemblerException(QString("Неверный формат команды: %1").arg(textLine));
            }
            break;

        case 4:
            // Can only be a label and a command and two operands
            if (IsRegister(line[0].toUpper())) {
                throw AssemblerException(QString("Регистр не может использоваться в качестве метки: %1").arg(textLine));
            } else if (IsLabel(line[0]) && IsCommand(line[1])) {
                codeLine.Label = line[0].toUpper();
                codeLine.Command = line[1].toUpper();
                codeLine.FirstOperand = line[2];
                codeLine.SecondOperand = line[3];
            } else {
                throw AssemblerException(QString("Неверный формат команды: %1").arg(textLine));
            }
            break;

        default:
            throw AssemblerException(QString("Неверный формат команды. Ни один из известных форматов не применим: %1").arg(textLine));
    }

    return codeLine;
}

