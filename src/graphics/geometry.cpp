#include "graphics/geometry.h"
#include "graphics/gl_ext.h"
#include "graphics/shader.h"

namespace Lux
{

void Geometry::draw(int start, int count)
{
	glBindBuffer(GL_ARRAY_BUFFER, m_id);         // for vertex coordinates
	glEnableClientState(GL_VERTEX_ARRAY);             // activate vertex coords array
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glEnableClientState(GL_NORMAL_ARRAY);
	const GLsizei stride = 16 * sizeof(GLfloat);
	glVertexPointer(3, GL_FLOAT, stride, (GLvoid*)(8 * sizeof(GLfloat)));               // last param is offset, not ptr
	glTexCoordPointer(2, GL_FLOAT, stride, (GLvoid*)(14 * sizeof(GLfloat)));
	glNormalPointer(GL_FLOAT, stride, (GLvoid*)(11 * sizeof(GLfloat)));
	glDrawArrays(GL_TRIANGLES, start, count);
	glDisableClientState(GL_VERTEX_ARRAY);            // deactivate vertex array
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_NORMAL_ARRAY);
}


Geometry::Geometry()
{
	glGenBuffers(1, &m_id);
}


Geometry::~Geometry()
{
	glDeleteBuffers(1, &m_id);
}


void Geometry::copy(const void* data, int size)
{
	glBindBuffer(GL_ARRAY_BUFFER, m_id);
	glBufferData(GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
}


} // ~namespace Lux
