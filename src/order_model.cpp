#include "order_model.hpp"

namespace korrelator {
order_model::order_model(QObject *parent) : QAbstractTableModel(parent) {}

QVariant order_model::headerData(int section, Qt::Orientation orientation,
                                 int role) const {
  if (role != Qt::DisplayRole)
    return QVariant{};

  if (orientation == Qt::Horizontal) {
    switch (section) {
    case 0:
      return "Correlator ID";
    case 1:
      return "Origin";
    case 2:
      return "Exchange";
    case 3:
      return "Symbol";
    case 4:
      return "Market type";
    case 5:
      return "Signal price";
    case 6:
      return "Signal date/time";
    case 7:
      return "Open price";
    case 8:
      return "Open date/time";
    case 9:
      return "Side";
    case 10:
      return "Exchange price";
    case 11:
      return "Remarks";
    default:
      return QVariant{};
    }
  }
  return section + 1;
}

int order_model::rowCount(const QModelIndex &parent) const {
  if (parent.isValid())
    return 0;
  return (int)m_modelData.size();
}

int order_model::columnCount(const QModelIndex &parent) const {
  if (parent.isValid())
    return 0;
  return 12;
}

model_data_t* order_model::modelDataFor(QString const &orderID,
                                        QString const &side) {
  for (auto &data: m_modelData) {
    if (orderID.compare(data.userOrderID, Qt::CaseInsensitive) == 0 &&
        side.compare(data.side, Qt::CaseInsensitive) == 0)
    {
      return &data;
    }
  }
  return nullptr;
}

void order_model::refreshModel() {
  QModelIndex const startModel = index(0, 0);
  QModelIndex const endModel = index((int)m_modelData.size() - 1,
                                     (int)columnCount() - 1);
  emit QAbstractTableModel::dataChanged(startModel, endModel);
}

QVariant order_model::data(const QModelIndex &index, int role) const {
  if (!index.isValid())
    return QVariant();

  if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
    auto const &d = m_modelData[index.row()];
    switch (index.column()) {
    case 0:
      return d.userOrderID;
    case 1:
      return d.tradeOrigin;
    case 2:
      return d.exchange;
    case 3:
      return d.symbol;
    case 4:
      return d.marketType;
    case 5:
      return d.signalPrice;
    case 6:
      return d.signalTime;
    case 7:
      return d.openPrice;
    case 8:
      return d.openTime;
    case 9:
      return d.side;
    case 10:
      return d.exchangePrice;
    case 11:
      return d.remark;
    default:
      return QVariant{};
    }
  } else if (role == Qt::TextAlignmentRole)
    return Qt::AlignCenter;
  return QVariant{};
}

Qt::ItemFlags order_model::flags(QModelIndex const &index) const {
  if (!index.isValid()) {
    return Qt::ItemIsEnabled;
  }
  return QAbstractTableModel::flags(index) | Qt::ItemIsEnabled |
         Qt::ItemIsSelectable;
}

bool order_model::insertRows(int row, int count, const QModelIndex &parent) {
  beginInsertRows(parent, row, row + count - 1);
  endInsertRows();
  return true;
}

void order_model::AddData(model_data_t const &data) {
  m_modelData.push_front(data);
  insertRows(0, 1);
}

} // namespace korrelator
