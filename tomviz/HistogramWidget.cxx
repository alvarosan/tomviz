/******************************************************************************

  This source file is part of the tomviz project.

  Copyright Kitware, Inc.

  This source code is released under the New BSD License, (the "License").

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

******************************************************************************/
#include "HistogramWidget.h"

#include "ActiveObjects.h"
#include "DataSource.h"
#include "ModuleContour.h"
#include "ModuleManager.h"
#include "Utilities.h"

#include "vtkChartHistogramColorOpacityEditor.h"

#include <vtkContextView.h>
#include <vtkContextScene.h>
#include <vtkControlPointsItem.h>
#include <vtkEventQtSlotConnect.h>
#include <vtkPiecewiseFunction.h>
#include <vtkRenderWindow.h>

#include <QVTKWidget.h>

#include <pqApplicationCore.h>
#include <pqCoreUtilities.h>
#include <pqPresetDialog.h>
#include <pqServerManagerModel.h>
#include <pqView.h>

#include <vtkPVDiscretizableColorTransferFunction.h>
#include <vtkSMPropertyHelper.h>
#include <vtkSMTransferFunctionProxy.h>
#include <vtkSMViewProxy.h>

#include <QColorDialog>
#include <QHBoxLayout>
#include <QToolButton>
#include <QVBoxLayout>

#include <QDebug>

namespace tomviz
{

HistogramWidget::HistogramWidget(QWidget *parent) : QWidget(parent),
  qvtk(new QVTKWidget(this))
{
  // Set up our little chart.
  this->HistogramView->SetInteractor(this->qvtk->GetInteractor());
  this->qvtk->SetRenderWindow(this->HistogramView->GetRenderWindow());
  this->HistogramView->GetScene()->AddItem(this->HistogramColorOpacityEditor.Get());

  // Connect events from the histogram color/opacity editor.
  this->EventLink->Connect(this->HistogramColorOpacityEditor.Get(),
                           vtkCommand::CursorChangedEvent,
                           this, SLOT(histogramClicked(vtkObject*)));
  this->EventLink->Connect(this->HistogramColorOpacityEditor.Get(),
                           vtkCommand::EndEvent,
                           this, SLOT(onScalarOpacityFunctionChanged()));
  this->EventLink->Connect(this->HistogramColorOpacityEditor.Get(),
                           vtkControlPointsItem::CurrentPointEditEvent,
                           this, SLOT(onCurrentPointEditEvent()));

  QHBoxLayout *hLayout = new QHBoxLayout(this);
  hLayout->addWidget(this->qvtk);
  QVBoxLayout *vLayout = new QVBoxLayout;
  hLayout->addLayout(vLayout);

  QToolButton *button = new QToolButton;
  button->setIcon(QIcon(":/pqWidgets/Icons/pqFavorites16.png"));
  button->setToolTip("Choose preset color map");
  connect(button, SIGNAL(clicked()), this, SLOT(onPresetClicked()));
  vLayout->addWidget(button);

  this->setLayout(hLayout);
}

HistogramWidget::~HistogramWidget()
{
}

void HistogramWidget::setLUT(vtkPVDiscretizableColorTransferFunction *lut)
{
  if (this->LUT != lut)
  {
    if (this->ScalarOpacityFunction)
    {
      this->EventLink->Disconnect(this->ScalarOpacityFunction,
                                  vtkCommand::ModifiedEvent,
                                  this, SLOT(onScalarOpacityFunctionChanged()));
    }
    this->LUT = lut;
    this->ScalarOpacityFunction = this->LUT->GetScalarOpacityFunction();
    this->EventLink->Connect(this->ScalarOpacityFunction,
                             vtkCommand::ModifiedEvent,
                             this, SLOT(onScalarOpacityFunctionChanged()));
  }
}

void HistogramWidget::setLUTProxy(vtkSMProxy *proxy)
{
  if (this->LUTProxy != proxy)
  {
    this->LUTProxy = proxy;
  }
}

void HistogramWidget::setInputData(vtkTable *table, const char *x, const char *y)
{
  this->HistogramColorOpacityEditor->SetHistogramInputData(table, x, y);
  this->HistogramColorOpacityEditor->SetOpacityFunction(this->ScalarOpacityFunction);
  if (this->LUT)
  {
    this->HistogramColorOpacityEditor->SetScalarVisibility(true);
    this->HistogramColorOpacityEditor->SetColorTransferFunction(this->LUT);
    this->HistogramColorOpacityEditor->SelectColorArray("image_extents");
  }
  this->HistogramView->Render();
}

void HistogramWidget::onScalarOpacityFunctionChanged()
{
  pqApplicationCore* core = pqApplicationCore::instance();
  pqServerManagerModel* smModel = core->getServerManagerModel();
  QList<pqView*> views = smModel->findItems<pqView*>();
  foreach(pqView* view, views)
  {
    view->render();
  }

  // Update the histogram
  this->HistogramView->GetRenderWindow()->Render();
}

void HistogramWidget::onCurrentPointEditEvent()
{
  double rgb[3];
  if (this->HistogramColorOpacityEditor->GetCurrentControlPointColor(rgb))
  {
    QColor color = QColorDialog::getColor(
      QColor::fromRgbF(rgb[0], rgb[1], rgb[2]), this,
      "Select Color for Control Point", QColorDialog::DontUseNativeDialog);
    if (color.isValid())
    {
      rgb[0] = color.redF();
      rgb[1] = color.greenF();
      rgb[2] = color.blueF();
      this->HistogramColorOpacityEditor->SetCurrentControlPointColor(rgb);
      this->onScalarOpacityFunctionChanged();
    }
  }
}

void HistogramWidget::histogramClicked(vtkObject *)
{
  DataSource *activeDataSource = ActiveObjects::instance().activeDataSource();
  Q_ASSERT(activeDataSource);

  vtkSMViewProxy* view = ActiveObjects::instance().activeView();
  if (!view)
  {
    return;
  }

  // Use active ModuleContour is possible. Otherwise, find the first existing
  // ModuleContour instance or just create a new one, if none exists.
  typedef ModuleContour ModuleContourType;

  ModuleContourType* contour = qobject_cast<ModuleContourType*>(
    ActiveObjects::instance().activeModule());
  if (!contour)
  {
    QList<ModuleContourType*> contours =
      ModuleManager::instance().findModules<ModuleContourType*>(activeDataSource, view);
    if (contours.size() == 0)
    {
      contour = qobject_cast<ModuleContourType*>(ModuleManager::instance().createAndAddModule(
          "Contour", activeDataSource, view));
    }
    else
    {
      contour = contours[0];
    }
    ActiveObjects::instance().setActiveModule(contour);
  }
  Q_ASSERT(contour);
  contour->setIsoValue(this->HistogramColorOpacityEditor->GetContourValue());
  tomviz::convert<pqView*>(view)->render();
}

void HistogramWidget::onPresetClicked()
{
  pqPresetDialog dialog(pqCoreUtilities::mainWidget(),
                        pqPresetDialog::SHOW_NON_INDEXED_COLORS_ONLY);
  dialog.setCustomizableLoadColors(true);
  dialog.setCustomizableLoadOpacities(true);
  dialog.setCustomizableUsePresetRange(true);
  dialog.setCustomizableLoadAnnotations(false);
  connect(&dialog, SIGNAL(applyPreset(const Json::Value&)),
          SLOT(applyCurrentPreset()));
  dialog.exec();
}

void HistogramWidget::applyCurrentPreset()
{
  pqPresetDialog* dialog = qobject_cast<pqPresetDialog*>(this->sender());
  Q_ASSERT(dialog);

  vtkSMProxy* lut = this->LUTProxy;
  if (!lut)
  {
    return;
  }

  if (dialog->loadColors() || dialog->loadOpacities())
  {
    vtkSMProxy* sof = vtkSMPropertyHelper(lut,
                                          "ScalarOpacityFunction",
                                          true).GetAsProxy();
    if (dialog->loadColors())
    {
      vtkSMTransferFunctionProxy::ApplyPreset(lut, dialog->currentPreset(),
                                              !dialog->usePresetRange());
    }
    if (dialog->loadOpacities())
    {
      if (sof)
      {
        vtkSMTransferFunctionProxy::ApplyPreset(
          sof, dialog->currentPreset(), !dialog->usePresetRange());
      }
      else
      {
        qWarning("Cannot load opacities since 'ScalarOpacityFunction' is not present.");
      }
    }

    // We need to take extra care to avoid the color and opacity function ranges
    // from straying away from each other. This can happen if only one of them is
    // getting a preset and we're using the preset range.
    if (dialog->usePresetRange()
        && (dialog->loadColors() ^ dialog->loadOpacities()) && sof)
    {
      double range[2];
      if (dialog->loadColors()
          && vtkSMTransferFunctionProxy::GetRange(lut, range))
      {
        vtkSMTransferFunctionProxy::RescaleTransferFunction(sof, range);
      }
      else if (dialog->loadOpacities()
               && vtkSMTransferFunctionProxy::GetRange(sof, range))
      {
        vtkSMTransferFunctionProxy::RescaleTransferFunction(lut, range);
      }
    }
  }
}

}
