import tkinter as tk

# Create the main window
root = tk.Tk()
root.title("Hello App")

# Create a label widget
label = tk.Label(root, text="Hello, World!", font=("Arial", 16))
label.pack(padx=20, pady=20)

# Run the application
root.mainloop()
