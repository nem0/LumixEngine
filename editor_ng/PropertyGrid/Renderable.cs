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
    public partial class Renderable : UserControl
    {
        private native.EditorServer m_editor_server;

        public Renderable(native.EditorServer server)
        {
            m_editor_server = server;
            InitializeComponent();
            sourceInput.Changed += sourceInput_Changed;
            sourceInput.Filter = "Scene XML Files|*.scene.xml|All Files|*.*";
            m_editor_server.onComponentProperties += m_editor_server_onComponentProperties;
            this.VisibleChanged += (object sender, EventArgs e) =>
            {
                m_editor_server.onComponentProperties -= m_editor_server_onComponentProperties;
                if (Visible)
                {
                    m_editor_server.onComponentProperties += m_editor_server_onComponentProperties;
                }
            };
        }

        void m_editor_server_onComponentProperties(object sender, EventArgs e)
        {
            BeginInvoke(new Action(() =>
            {
                native.EditorServer.ComponentPropertiesArgs args = e as native.EditorServer.ComponentPropertiesArgs;
                if (args.cmp_type == Crc32.Compute("renderable"))
                {
                    for (int i = 0; i < args.names.Length; ++i)
                    {
                        if (args.names[i] == Crc32.Compute("source"))
                        {
                            sourceInput.Value = System.Text.Encoding.ASCII.GetString(args.values[i]);
                        }
                        else if (args.names[i] == Crc32.Compute("visible"))
                        {
                            visibleCheckBox.Checked = BitConverter.ToBoolean(args.values[i], 0);
                        }
                        else if (args.names[i] == Crc32.Compute("cast shadows"))
                        {
                            castShadowsCheckbox.Checked = BitConverter.ToBoolean(args.values[i], 0);
                        }
                    }
                }
            }));
        }

        void sourceInput_Changed(object sender, EventArgs e)
        {
            m_editor_server.setComponentProperty("renderable", "source", sourceInput.Value);
        }

        private void visibleCheckBox_CheckedChanged(object sender, EventArgs e)
        {
            m_editor_server.setComponentProperty("renderable", "visible", visibleCheckBox.Checked);
        }

        private void castShadowsCheckbox_CheckedChanged(object sender, EventArgs e)
        {
            m_editor_server.setComponentProperty("renderable", "cast shadows", castShadowsCheckbox.Checked);
        }
    }
}
