namespace editor_ng.PropertyGrid
{
    partial class PointLight
    {
        /// <summary> 
        /// Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary> 
        /// Clean up any resources being used.
        /// </summary>
        /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Component Designer generated code

        /// <summary> 
        /// Required method for Designer support - do not modify 
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            this.label1 = new System.Windows.Forms.Label();
            this.radiusNumericUpDown = new System.Windows.Forms.NumericUpDown();
            this.label2 = new System.Windows.Forms.Label();
            this.fovNumericUpDown = new System.Windows.Forms.NumericUpDown();
            ((System.ComponentModel.ISupportInitialize)(this.radiusNumericUpDown)).BeginInit();
            ((System.ComponentModel.ISupportInitialize)(this.fovNumericUpDown)).BeginInit();
            this.SuspendLayout();
            // 
            // label1
            // 
            this.label1.AutoSize = true;
            this.label1.Location = new System.Drawing.Point(1, 1);
            this.label1.Margin = new System.Windows.Forms.Padding(1, 1, 1, 3);
            this.label1.Name = "label1";
            this.label1.Size = new System.Drawing.Size(40, 13);
            this.label1.TabIndex = 0;
            this.label1.Text = "Radius";
            // 
            // radiusNumericUpDown
            // 
            this.radiusNumericUpDown.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.radiusNumericUpDown.DecimalPlaces = 3;
            this.radiusNumericUpDown.Font = new System.Drawing.Font("Microsoft Sans Serif", 6.75F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(238)));
            this.radiusNumericUpDown.Location = new System.Drawing.Point(50, 0);
            this.radiusNumericUpDown.Margin = new System.Windows.Forms.Padding(1);
            this.radiusNumericUpDown.Maximum = new decimal(new int[] {
            360,
            0,
            0,
            0});
            this.radiusNumericUpDown.Name = "radiusNumericUpDown";
            this.radiusNumericUpDown.Size = new System.Drawing.Size(84, 18);
            this.radiusNumericUpDown.TabIndex = 1;
            this.radiusNumericUpDown.ValueChanged += new System.EventHandler(this.radiusNumericUpDown_ValueChanged);
            // 
            // label2
            // 
            this.label2.AutoSize = true;
            this.label2.Location = new System.Drawing.Point(1, 22);
            this.label2.Margin = new System.Windows.Forms.Padding(1, 1, 1, 3);
            this.label2.Name = "label2";
            this.label2.Size = new System.Drawing.Size(28, 13);
            this.label2.TabIndex = 2;
            this.label2.Text = "FOV";
            // 
            // fovNumericUpDown
            // 
            this.fovNumericUpDown.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.fovNumericUpDown.DecimalPlaces = 3;
            this.fovNumericUpDown.Font = new System.Drawing.Font("Microsoft Sans Serif", 6.75F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(238)));
            this.fovNumericUpDown.Location = new System.Drawing.Point(49, 21);
            this.fovNumericUpDown.Margin = new System.Windows.Forms.Padding(1);
            this.fovNumericUpDown.Maximum = new decimal(new int[] {
            999999,
            0,
            0,
            0});
            this.fovNumericUpDown.Name = "fovNumericUpDown";
            this.fovNumericUpDown.Size = new System.Drawing.Size(84, 18);
            this.fovNumericUpDown.TabIndex = 3;
            this.fovNumericUpDown.ValueChanged += new System.EventHandler(this.fovNumericUpDown_ValueChanged);
            // 
            // PointLight
            // 
            this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.Controls.Add(this.fovNumericUpDown);
            this.Controls.Add(this.label2);
            this.Controls.Add(this.radiusNumericUpDown);
            this.Controls.Add(this.label1);
            this.Name = "PointLight";
            this.Size = new System.Drawing.Size(134, 42);
            ((System.ComponentModel.ISupportInitialize)(this.radiusNumericUpDown)).EndInit();
            ((System.ComponentModel.ISupportInitialize)(this.fovNumericUpDown)).EndInit();
            this.ResumeLayout(false);
            this.PerformLayout();

        }

        #endregion

        private System.Windows.Forms.Label label1;
        private System.Windows.Forms.NumericUpDown radiusNumericUpDown;
        private System.Windows.Forms.Label label2;
        private System.Windows.Forms.NumericUpDown fovNumericUpDown;
    }
}
