using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.Data;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace editor_ng.PropertyGrid
{
    public partial class Script : UserControl
    {
        native.EditorServer m_editor_server;
        public Script(native.EditorServer server)
        {
            m_editor_server = server;
            InitializeComponent();
            m_editor_server.onComponentProperties += m_editor_server_onComponentProperties;
            sourceFileInput.Changed += sourceFileInput_Changed;
            sourceFileInput.Filter = "Script Files|*.cpp|All Files|*.*";
            this.VisibleChanged += (object sender, EventArgs e) =>
            {
                m_editor_server.onComponentProperties -= m_editor_server_onComponentProperties;
                if (Visible)
                {
                    m_editor_server.onComponentProperties += m_editor_server_onComponentProperties;
                }
            };
        }

        void sourceFileInput_Changed(object sender, EventArgs e)
        {
            m_editor_server.setComponentProperty("script", "source", sourceFileInput.Value);
        }

        void m_editor_server_onComponentProperties(object sender, EventArgs e)
        {
            BeginInvoke(new Action(() =>
            {
                native.EditorServer.ComponentPropertiesArgs args = e as native.EditorServer.ComponentPropertiesArgs;
                for (int i = 0; i < args.names.Length; ++i)
                {
                    if (args.names[i] == Crc32.Compute("source"))
                    {
                        sourceFileInput.Value = System.Text.Encoding.ASCII.GetString(args.values[i]);
                    }
                }
            }));
        }
    }
}
