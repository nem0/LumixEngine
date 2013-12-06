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
                        switch (args.names[i])
                        {
                            case "source":
                                sourceInput.Value = args.values[i];
                                break;
                            case "visible":
                                visibleCheckBox.Checked = args.values[i] == "true";
                                break;
                            case "cast shadows":
                                castShadowsCheckbox.Checked = args.values[i] == "true";
                                break;
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
            m_editor_server.setComponentProperty("renderable", "visible", visibleCheckBox.Checked ? "true" : "false");
        }

        private void castShadowsCheckbox_CheckedChanged(object sender, EventArgs e)
        {
            m_editor_server.setComponentProperty("renderable", "cast shadows", castShadowsCheckbox.Checked ? "true" : "false");
        }
    }
}
