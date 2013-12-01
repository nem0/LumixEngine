using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using WeifenLuo.WinFormsUI.Docking;

namespace editor_ng
{

    public partial class SceneView : DockContent
    {
        private int m_last_mouse_x;
        private int m_last_mouse_y;

        public native.EditorServer server { get; set; }

        public SceneView()
        {
            InitializeComponent();
        }

        public float cameraSpeed
        {
            get { return (float)cameraSpeedInput.Value; }
        }

        private void panel1_Paint(object sender, PaintEventArgs e)
        {
            server.draw();
        }

        private void panel1_Resize(object sender, EventArgs e)
        {
            server.resize();
        }

        private void panel1_MouseDown(object sender, MouseEventArgs e)
        {
            panel1.Focus();
            server.mouseDown(e.X, e.Y, e.Button == MouseButtons.Left ? 0 : 2);
            m_last_mouse_x = e.X;
            m_last_mouse_y = e.Y;
            if (e.Button == MouseButtons.Right)
                System.Windows.Forms.Cursor.Hide();
        }

        private void panel1_MouseMove(object sender, MouseEventArgs e)
        {
            server.mouseMove(e.X, e.Y, e.X - m_last_mouse_x, e.Y - m_last_mouse_y, e.Button == MouseButtons.Left ? 0 : 2);
            m_last_mouse_x = e.X;
            m_last_mouse_y = e.Y;
        }

        private void panel1_MouseUp(object sender, MouseEventArgs e)
        {
            server.mouseUp(e.X, e.Y, e.Button == MouseButtons.Left ? 0 : 2);
            if (e.Button == MouseButtons.Right)
                System.Windows.Forms.Cursor.Show();
        }
    }

    public class UserDrawnPanel : Panel
    {
        public UserDrawnPanel()
        {
            SetStyle(ControlStyles.UserPaint, true);
        }

        protected override void OnPaintBackground(PaintEventArgs e)
        {
        }
    }
}
