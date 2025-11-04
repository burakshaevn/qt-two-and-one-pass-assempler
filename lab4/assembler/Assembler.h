#ifndef ASSEMBLER_H
#define ASSEMBLER_H

#include <QString>
#include <QList>
#include "Command.h"
#include "CommandDto.h"
#include "CodeLine.h"
#include "SymbolicName.h"
#include "AssemblerException.h"

class Assembler
{
public:
    QList<QList<QString>> SourceCode;
    QList<QString> BinaryCode;
    int lineIterator;
    QList<Command> AvailibleCommands;
    QList<SymbolicName> TSI;

    Assembler();
    void SetAvailibleCommands(const QList<CommandDto>& newAvailibleCommandsDto);
    void Reset(const QList<QList<QString>>& sourceCode, const QList<CommandDto>& newCommands);
    bool ProcessStep();

private:
    static const int maxAddress = 16777215;  // 2^24 - 1
    int startAddress;
    int endAddress;
    bool startFlag;
    bool endFlag;
    int ip;

    static const QStringList AvailibleDirectives;

    void ClearTSI();
    bool IsCommand(const QString& chunk) const;
    bool IsDirective(const QString& chunk) const;
    bool IsLabel(const QString& chunk) const;
    static bool IsXString(const QString& chunk);
    static bool IsCString(const QString& chunk);
    static bool IsRegister(const QString& chunk);
    static int GetRegisterNumber(const QString& chunk);
    SymbolicName* GetSymbolicName(const QString& chunk);
    static QString ConvertToASCII(const QString& chunk);
    static void OverflowCheck(int value, const QString& textLine);
    CodeLine GetCodeLineFromSource(const QList<QString>& line);
    void CheckAddressRequirements();
    void ProvideAddresses(SymbolicName* symbolicName);
};

#endif // ASSEMBLER_H

