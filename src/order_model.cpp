#include "order_model.hpp"

namespace korrelator {
order_model::order_model(QObject *parent)
  : QAbstractTableModel(parent)
{
}

QVariant order_model::headerData(int section, Qt::Orientation orientation, int role) const
{
  if (role != Qt::DisplayRole)
    return QVariant{};

  if (orientation == Qt::Horizontal) {
    switch(section) {
    case 0: return "Symbol";
    case 1: return "Market type";
    case 2: return "Signal price";
    case 3: return "Signal date/time";
    case 4: return "Open price";
    case 5: return "Open date/time";
    case 6: return "Side";
    default: return QVariant{};
    }
  }
  return section + 1;
}

int order_model::rowCount(const QModelIndex &parent) const
{
  if (parent.isValid())
    return 0;
  return m_modelData.size();
}

int order_model::columnCount(const QModelIndex &parent) const
{
  if (parent.isValid())
    return 0;
  return 7;
}

QVariant order_model::data(const QModelIndex &index, int role) const
{
  if (!index.isValid())
    return QVariant();

  if (role == Qt::DisplayRole) {
    auto const &d = m_modelData[index.row()];
    switch (index.column()) {
    case 0: return d.symbol;
    case 1: return d.marketType;
    case 2: return d.signalPrice;
    case 3: return d.signalTime;
    case 4: return d.openPrice;
    case 5: return d.openTime;
    case 6: return d.side;
    default: return QVariant{};
    }
  } else if (role == Qt::TextAlignmentRole)
    return Qt::AlignCenter;
  return QVariant{};
}

Qt::ItemFlags order_model::flags( QModelIndex const &index ) const
{
  if( !index.isValid() ){
    return Qt::ItemIsEnabled;
  }
  return QAbstractTableModel::flags(index) |
      Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

bool order_model::insertRows(int row, int count, const QModelIndex &parent)
{
  beginInsertRows(parent, row, row + count - 1);
  endInsertRows();
  return true;
}

void order_model::AddData(model_data_t &&data) {
  m_modelData.push_front(std::move(data));
  insertRows(0, 1);
}

}
