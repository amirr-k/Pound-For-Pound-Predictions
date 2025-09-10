import requests
from bs4 import BeautifulSoup
import time


def fetchWikipediaPage(url):
    # Add headers to avoid being blocked
    headers = {
        'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36'
    }
    try:
        print(f"Fetching Wikipedia page: {url}")
        response = requests.get(url, headers=headers, timeout=10)
        response.raise_for_status()
        
        # Parse through the HTML
        soup = BeautifulSoup(response.text, 'html.parser')
        return soup
    except Exception as e:
        print(f"Error fetching Wikipedia page: {e}")
        return None

if __name__ == "__main__":
    # Test with Mike Tyson the GOAT frfrfr
    testUrl = "https://en.wikipedia.org/wiki/Mike_Tyson"
    soup = fetchWikipediaPage(testUrl)
    if (soup):
        title = soup.find('h1', class_='firstHeading')
        if (title):
            print(f"Sucessfully fetched page: {title.get_text()}")
        else:
            print("Fetched page but couldn't find the title")
    else:
        print("Failed to fetch Wikipedia page")