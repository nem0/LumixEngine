using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using WeifenLuo.WinFormsUI.Docking;


namespace editor_ng.Log
{
    public partial class LogUI : DockContent
    {
        native.EditorServer m_server;
        Image[] m_icons = new Image[3];

        public LogUI(native.EditorServer server)
        {
            m_server = server;
            m_server.onLogMessage += onLogMessage;
            InitializeComponent();
            try
            {
                m_icons[0] = Bitmap.FromFile("editor/info.png");
                m_icons[1] = Bitmap.FromFile("editor/warning.png");
                m_icons[2] = Bitmap.FromFile("editor/error.png");
            }
            catch (Exception)
            {
            }
        }

        void onLogMessage(object sender, EventArgs args)
        {
            native.EditorServer.LegMessageEventArgs e = args as native.EditorServer.LegMessageEventArgs;
            
            var objs = new object[] { m_icons[e.type], DateTime.Now.ToString("HH:mm:ss.fff"), e.system, e.message };
            dataGridView1.Rows.Add(objs);
        }
    }
}
