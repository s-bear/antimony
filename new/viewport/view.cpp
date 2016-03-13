#include <Python.h>

#include <QOpenGLWidget>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QMenu>

#include "viewport/view.h"
#include "viewport/scene.h"
#include "viewport/image.h"
#include "viewport/control/control.h"

#include "app/colors.h"

#include "graph/proxy/datum.h"
#include "graph/constructor/populate.h"

ViewportView::ViewportView(QWidget* parent, ViewportScene* scene)
    : QGraphicsView(new QGraphicsScene(), parent), gl(new QOpenGLWidget(this)),
      scale(100), pitch(0), yaw(0), view_scene(scene)
{
    setStyleSheet("QGraphicsView { border-style: none; }");
    setRenderHints(QPainter::Antialiasing);

    setViewport(gl.context);

    setSceneRect(-width()/2, -height()/2, width(), height());

    QAbstractScrollArea::setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QAbstractScrollArea::setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

////////////////////////////////////////////////////////////////////////////////

QMatrix4x4 ViewportView::getMatrix() const
{
    QMatrix4x4 M;

    // Remember that these operations are applied in reverse order.
    M.scale(scale, -scale, scale);
    M.rotate(pitch * 180 / M_PI, QVector3D(1, 0, 0));
    M.rotate(yaw  *  180 / M_PI, QVector3D(0, 0, 1));
    M.translate(-center.x(), -center.y(), -center.z());

    return M;
}

QVector3D ViewportView::sceneToWorld(QPointF p) const
{
    return getMatrix().inverted() * QVector3D(p.x(), p.y(), 0);
}

void ViewportView::installImage(DepthImage* d)
{
    connect(this, &ViewportView::getDepth,
            d, &DepthImage::getDepth);
    connect(this, &ViewportView::paintImage,
            d, &DepthImage::paint);
}

////////////////////////////////////////////////////////////////////////////////

void ViewportView::drawBackground(QPainter* painter, const QRectF& rect)
{
    QGraphicsView::drawBackground(painter, rect);

    painter->beginNativePainting();
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Get bounds from all child images
    float zmin = INFINITY;
    float zmax = -INFINITY;
    auto m = getMatrix();
    emit(getDepth(m, &zmin, &zmax));

    // Paint all images
    emit(paintImage(m, zmin, zmax));

    painter->endNativePainting();
}

void ViewportView::drawAxes(QPainter* painter) const
{
    // First, draw the axes.
    auto m = getMatrix();
    QVector3D o = m * QVector3D(0, 0, 0);
    QVector3D x = m * QVector3D(1, 0, 0);
    QVector3D y = m * QVector3D(0, 1, 0);
    QVector3D z = m * QVector3D(0, 0, 1);

    QList<QPair<QVector3D, QColor>> pts = {
        {x, Colors::red},
        {y, Colors::green},
        {z, Colors::blue}};

    // Sort the axes to fake proper z clipping
    std::sort(pts.begin(), pts.end(),
            [](QPair<QVector3D, QColor> a, QPair<QVector3D, QColor> b)
            { return a.first.z() < b.first.z(); });

    for (auto p : pts)
    {
        painter->setPen(QPen(p.second, 2));
        painter->drawLine(o.toPointF(), p.first.toPointF());
    }
}

void ViewportView::drawForeground(QPainter* painter, const QRectF& rect)
{
    QGraphicsView::drawForeground(painter, rect);
    drawAxes(painter);
}

////////////////////////////////////////////////////////////////////////////////

void ViewportView::update()
{
    QGraphicsView::update();
    emit(changed(getMatrix()));
}

////////////////////////////////////////////////////////////////////////////////

void ViewportView::installControl(Control* c)
{
    c->makeInstanceFor(this);
}

void ViewportView::installDatum(BaseDatumProxy* d)
{
    d->addViewport(this);
}

////////////////////////////////////////////////////////////////////////////////

void ViewportView::mousePressEvent(QMouseEvent* event)
{
    QGraphicsView::mousePressEvent(event);

    // If the event hasn't been accepted, record click position for
    // panning / rotation on mouse drag.
    if (!event->isAccepted())
    {
        if (event->button() == Qt::LeftButton)
        {
            click_pos = mapToScene(event->pos());
            click_pos_world = sceneToWorld(click_pos);
        }
        else
        {
            click_pos = event->pos();
        }
        dragged = false;
    }
}

void ViewportView::mouseMoveEvent(QMouseEvent* event)
{
    QGraphicsView::mouseMoveEvent(event);

    current_pos = event->pos();
    if (scene()->mouseGrabberItem() == NULL)
    {
        if (event->buttons() == Qt::LeftButton)
        {
            center += click_pos_world - sceneToWorld(mapToScene(event->pos()));
            update();
        }
        else if (event->buttons() == Qt::RightButton)
        {
            QPointF d = click_pos - event->pos();
            pitch = fmin(0, fmax(-M_PI, pitch - 0.01 * d.y()));
            yaw = fmod(yaw + M_PI - 0.01 * d.x(), M_PI*2) - M_PI;

            click_pos = event->pos();
            update();
        }
        dragged = true;
    }
}

void ViewportView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton && !dragged)
    {
        openAddMenu();
    }
}

void ViewportView::wheelEvent(QWheelEvent* event)
{
    QVector3D a = sceneToWorld(mapToScene(event->pos()));
    scale *= pow(1.001, -event->delta());
    QVector3D b = sceneToWorld(mapToScene(event->pos()));
    center += a - b;
    update();
}

void ViewportView::resizeEvent(QResizeEvent* e)
{
    Q_UNUSED(e);
    setSceneRect(-width()/2, -height()/2, width(), height());
}

void ViewportView::keyPressEvent(QKeyEvent* event)
{
    QGraphicsView::keyPressEvent(event);
    if (event->isAccepted())
    {
        return;
    }
    if (event->key() == Qt::Key_A &&
        (event->modifiers() & Qt::ShiftModifier))
    {
        openAddMenu();
    }
}

////////////////////////////////////////////////////////////////////////////////

void ViewportView::openAddMenu()
{
    QMenu* m = new QMenu(this);
    populateNodeMenu(m, view_scene->getGraph());

    m->exec(QCursor::pos());
    m->deleteLater();
}
