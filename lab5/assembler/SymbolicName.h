#ifndef SYMBOLICNAME_H
#define SYMBOLICNAME_H

#include <QString>
#include <QList>

class SymbolicName
{
public:
    QString Name;
    int Address;  // -1 means undefined
    QList<int> AddressRequirements;

    SymbolicName();
    bool isDefined() const { return Address != -1; }
};

#endif // SYMBOLICNAME_H

