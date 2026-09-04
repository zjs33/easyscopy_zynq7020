#include "acquisition/idatasource.h"

IDataSource::IDataSource(QObject *parent)
    : QObject(parent)
{
}

IDataSource::~IDataSource() = default;
