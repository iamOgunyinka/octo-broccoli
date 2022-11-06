#pragma once

#include <QAbstractTableModel>
#include <QMetaType>
#include <deque>

namespace korrelator {

struct model_data_t {
  model_data_t* friendModel = nullptr;
  QString exchange;
  QString userOrderID;
  QString symbol;
  QString marketType;
  QString signalTime;
  QString openTime;
  QString side;
  QString remark;
  QString tradeOrigin;
  double signalPrice = 0.0;
  double openPrice = 0.0;
  double exchangePrice = 0.0;
};

class order_model : public QAbstractTableModel {
  Q_OBJECT

public:
  explicit order_model(QObject *parent = nullptr);

  // Header:
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role = Qt::DisplayRole) const override;
  Qt::ItemFlags flags(QModelIndex const &index) const override;
  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;

  bool insertRows(int row, int count,
                  const QModelIndex &parent = QModelIndex()) override;
  void AddData(model_data_t const &);
  void refreshModel ();
  auto totalRows() const { return m_modelData.size(); }
  auto allItems() const {
    return std::vector<model_data_t>(m_modelData.cbegin(), m_modelData.cend());
  }
  model_data_t* modelDataFor(QString const &, QString const &);
  model_data_t* front() {
    if (m_modelData.empty())
      return nullptr;
    return &m_modelData.front();
  }

private:
  std::deque<model_data_t> m_modelData;
};

} // namespace korrelator

Q_DECLARE_METATYPE(korrelator::model_data_t);
