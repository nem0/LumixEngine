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

        public LogUI(native.EditorServer server)
        {
            m_server = server;
            m_server.onLogMessage += onLogMessage;
            InitializeComponent();
        }

        void onLogMessage(object sender, EventArgs e)
        {
            dataGridView1.Rows.Add(new [] { sender as string});
        }
    }
}
